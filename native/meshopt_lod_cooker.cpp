#include "meshoptimizer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

namespace {

constexpr std::array<uint8_t, 8> INPUT_MAGIC = {'E', 'L', 'I', 'S', 'A', 'L', 'D', '1'};
constexpr std::array<uint8_t, 8> OUTPUT_MAGIC = {'E', 'L', 'I', 'S', 'A', 'L', 'R', '1'};
constexpr size_t HEADER_BYTES = 28;
constexpr size_t RANGE_BYTES = 12;
constexpr size_t MAX_VERTICES = 2'000'000;
constexpr size_t MAX_INDICES = 15'000'000;
constexpr size_t MAX_RANGES = 16 * 256;
constexpr size_t MAX_PACKET_BYTES = 128 * 1024 * 1024;
constexpr float MAX_ERROR = 0.02f;
constexpr size_t IO_BLOCK_BYTES = 64 * 1024;

uint32_t read_u32(const std::vector<uint8_t>& bytes, size_t offset) {
    return uint32_t(bytes[offset]) | (uint32_t(bytes[offset + 1]) << 8) |
        (uint32_t(bytes[offset + 2]) << 16) | (uint32_t(bytes[offset + 3]) << 24);
}

float read_f32(const std::vector<uint8_t>& bytes, size_t offset) {
    const uint32_t bits = read_u32(bytes, offset);
    float value = 0.0f;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

void append_u32(std::vector<uint8_t>& bytes, uint32_t value) {
    bytes.push_back(uint8_t(value));
    bytes.push_back(uint8_t(value >> 8));
    bytes.push_back(uint8_t(value >> 16));
    bytes.push_back(uint8_t(value >> 24));
}

void append_f32(std::vector<uint8_t>& bytes, float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    append_u32(bytes, bits);
}

bool reject(const char* message) {
    std::fprintf(stderr, "meshoptimizer LOD cooker: %s\n", message);
    return false;
}

bool run() {
    std::vector<uint8_t> input;
    std::array<char, IO_BLOCK_BYTES> block{};
    while (std::cin) {
        std::cin.read(block.data(), static_cast<std::streamsize>(block.size()));
        const std::streamsize count = std::cin.gcount();
        if (count <= 0) continue;
        const size_t amount = static_cast<size_t>(count);
        if (amount > MAX_PACKET_BYTES - input.size()) return reject("input exceeds the packet bound");
        input.insert(input.end(), block.begin(), block.begin() + count);
    }
    if (std::cin.bad()) return reject("failed to read packet input");
    if (input.size() < HEADER_BYTES ||
        !std::equal(INPUT_MAGIC.begin(), INPUT_MAGIC.end(), input.begin())) {
        return reject("invalid packet header");
    }

    const uint32_t vertex_count = read_u32(input, 8);
    const uint32_t index_count = read_u32(input, 12);
    const uint32_t range_count = read_u32(input, 16);
    const float ratio = read_f32(input, 20);
    const float max_error = read_f32(input, 24);
    if (vertex_count == 0 || vertex_count > MAX_VERTICES || index_count == 0 ||
        index_count > MAX_INDICES || index_count % 3 != 0 || range_count == 0 ||
        range_count > MAX_RANGES || !std::isfinite(ratio) || ratio <= 0.0f || ratio >= 1.0f ||
        !std::isfinite(max_error) || max_error < 0.0f || max_error > MAX_ERROR) {
        return reject("counts, ratio, or error exceed the supported limits");
    }

    const size_t ranges_offset = HEADER_BYTES;
    const size_t positions_offset = ranges_offset + size_t(range_count) * RANGE_BYTES;
    const size_t positions_bytes = size_t(vertex_count) * 3 * sizeof(float);
    const size_t indices_offset = positions_offset + positions_bytes;
    if (positions_offset > input.size() || positions_bytes > input.size() - positions_offset ||
        indices_offset > input.size() || size_t(index_count) >
            (input.size() - indices_offset) / sizeof(uint32_t) ||
        indices_offset + size_t(index_count) * sizeof(uint32_t) != input.size()) {
        return reject("packet byte count does not match its bounded counts");
    }

    uint32_t covered = 0;
    for (uint32_t range = 0; range < range_count; ++range) {
        const size_t offset = ranges_offset + size_t(range) * RANGE_BYTES;
        const uint32_t start = read_u32(input, offset);
        const uint32_t count = read_u32(input, offset + 4);
        const uint32_t target = read_u32(input, offset + 8);
        if (start != covered || count == 0 || count % 3 != 0 || count > index_count - covered ||
            target < 3 || target > count || target % 3 != 0) {
            return reject("ranges must partition triangle indices and have valid triangle targets");
        }
        covered += count;
    }
    if (covered != index_count) return reject("ranges do not cover the index stream");

    std::vector<float> positions(size_t(vertex_count) * 3);
    for (size_t index = 0; index < positions.size(); ++index) {
        positions[index] = read_f32(input, positions_offset + index * sizeof(float));
        if (!std::isfinite(positions[index])) return reject("position stream is not finite");
    }
    for (uint32_t index = 0; index < index_count; ++index) {
        if (read_u32(input, indices_offset + size_t(index) * sizeof(uint32_t)) >= vertex_count) {
            return reject("index is outside the vertex stream");
        }
    }

    std::vector<uint8_t> output;
    output.insert(output.end(), OUTPUT_MAGIC.begin(), OUTPUT_MAGIC.end());
    append_u32(output, range_count);
    for (uint32_t range = 0; range < range_count; ++range) {
        const size_t range_offset = ranges_offset + size_t(range) * RANGE_BYTES;
        const uint32_t start = read_u32(input, range_offset);
        const uint32_t count = read_u32(input, range_offset + 4);
        const uint32_t target = read_u32(input, range_offset + 8);
        std::vector<uint32_t> source(count);
        for (uint32_t index = 0; index < count; ++index) {
            source[index] = read_u32(input, indices_offset + size_t(start + index) * sizeof(uint32_t));
        }
        std::vector<uint32_t> simplified(count);
        float result_error = 0.0f;
        size_t simplified_count = count;
        if (target < count) {
            simplified_count = meshopt_simplify(simplified.data(), source.data(), count,
                positions.data(), vertex_count, sizeof(float) * 3, target, max_error,
                meshopt_SimplifyLockBorder | meshopt_SimplifySparse, &result_error);
        } else {
            simplified = source;
        }
        if (simplified_count < 3 || simplified_count > target || simplified_count % 3 != 0 ||
            !std::isfinite(result_error) || result_error < 0.0f || result_error > max_error) {
            std::fprintf(stderr, "range %u: source=%u target=%u result=%zu error=%.6f limit=%.6f\n",
                range, count, target, simplified_count, result_error, max_error);
            return reject("simplification could not meet the per-subset triangle/error limits");
        }
        append_u32(output, static_cast<uint32_t>(simplified_count));
        append_f32(output, result_error);
        for (size_t index = 0; index < simplified_count; ++index) append_u32(output, simplified[index]);
    }
    std::cout.write(reinterpret_cast<const char*>(output.data()), static_cast<std::streamsize>(output.size()));
    return std::cout.good() || reject("failed to write response");
}

} // namespace

int main() {
    return run() ? 0 : 1;
}
