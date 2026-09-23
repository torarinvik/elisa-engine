#include "meshoptimizer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

constexpr uint8_t PACKET_MAGIC[8] = {'E', 'L', 'I', 'S', 'A', 'M', 'O', '1'};
constexpr size_t PACKET_HEADER_BYTES = 20;
constexpr size_t RANGE_BYTES = 12;
constexpr size_t INDEX_BYTES = sizeof(uint32_t);
constexpr uint32_t MAX_VERTICES = 2'000'000;
constexpr uint32_t MAX_INDICES = 15'000'000;
constexpr uint32_t MAX_SUBSETS = 16;
constexpr uint32_t MAX_PLACEMENTS = 256;
constexpr uint32_t MAX_RANGES = MAX_SUBSETS * MAX_PLACEMENTS;
constexpr uint32_t CACHE_SIZE = 16;
constexpr float ACMR_EPSILON = 0.0001f;
constexpr size_t REPORT_BYTES = 16;
constexpr size_t IO_BLOCK_BYTES = 64 * 1024;
static_assert(REPORT_BYTES == sizeof(float) * 2 + sizeof(uint32_t) * 2);
constexpr size_t MAX_PACKET_BYTES = PACKET_HEADER_BYTES + size_t(MAX_RANGES) * RANGE_BYTES +
    size_t(MAX_INDICES) * INDEX_BYTES;

uint32_t read_u32(const std::vector<uint8_t>& bytes, size_t offset) {
    return uint32_t(bytes[offset]) | (uint32_t(bytes[offset + 1]) << 8) |
        (uint32_t(bytes[offset + 2]) << 16) | (uint32_t(bytes[offset + 3]) << 24);
}

void write_u32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    bytes[offset] = uint8_t(value);
    bytes[offset + 1] = uint8_t(value >> 8);
    bytes[offset + 2] = uint8_t(value >> 16);
    bytes[offset + 3] = uint8_t(value >> 24);
}

void append_u32(std::vector<uint8_t>& bytes, uint32_t value) {
    const size_t offset = bytes.size();
    bytes.resize(offset + sizeof(uint32_t));
    write_u32(bytes, offset, value);
}

void append_float(std::vector<uint8_t>& bytes, float value) {
    uint32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    append_u32(bytes, bits);
}

bool reject(const char* message) {
    std::fprintf(stderr, "meshoptimizer glTF cooker: %s\n", message);
    return false;
}

bool run() {
    std::vector<uint8_t> input;
    std::array<char, IO_BLOCK_BYTES> block{};
    while (std::cin) {
        std::cin.read(block.data(), static_cast<std::streamsize>(block.size()));
        const std::streamsize read_count = std::cin.gcount();
        if (read_count <= 0) continue;
        const size_t amount = static_cast<size_t>(read_count);
        if (amount > MAX_PACKET_BYTES - input.size()) return reject("input exceeds the packet budget");
        input.insert(input.end(), block.begin(), block.begin() + read_count);
    }
    if (std::cin.bad()) return reject("failed to read packet input");
    if (input.size() < PACKET_HEADER_BYTES ||
        !std::equal(std::begin(PACKET_MAGIC), std::end(PACKET_MAGIC), input.begin())) {
        return reject("invalid packet header");
    }

    const uint32_t vertex_count = read_u32(input, 8);
    const uint32_t index_count = read_u32(input, 12);
    const uint32_t range_count = read_u32(input, 16);
    if (vertex_count == 0 || vertex_count > MAX_VERTICES || index_count == 0 ||
        index_count > MAX_INDICES || index_count % 3 != 0 || range_count == 0 ||
        range_count > MAX_RANGES) {
        return reject("vertex, index, or chunk counts exceed runtime limits");
    }
    const size_t ranges_offset = PACKET_HEADER_BYTES;
    const size_t indices_offset = ranges_offset + size_t(range_count) * RANGE_BYTES;
    if (indices_offset > input.size() || size_t(index_count) >
        (input.size() - indices_offset) / INDEX_BYTES ||
        indices_offset + size_t(index_count) * INDEX_BYTES != input.size()) {
        return reject("packet byte count does not match its bounded counts");
    }

    uint32_t covered_indices = 0;
    for (uint32_t range_index = 0; range_index < range_count; ++range_index) {
        const size_t offset = ranges_offset + size_t(range_index) * RANGE_BYTES;
        const uint32_t start = read_u32(input, offset);
        const uint32_t count = read_u32(input, offset + 4);
        const uint32_t material_slot = read_u32(input, offset + 8);
        if (start != covered_indices || count == 0 || count % 3 != 0 ||
            count > index_count - covered_indices || material_slot >= MAX_SUBSETS) {
            return reject("chunks must partition triangle indices within material limits");
        }
        covered_indices += count;
    }
    if (covered_indices != index_count) return reject("chunks do not cover the index stream");

    for (uint32_t index = 0; index < index_count; ++index) {
        if (read_u32(input, indices_offset + size_t(index) * INDEX_BYTES) >= vertex_count) {
            return reject("index is outside the vertex stream");
        }
    }

    double before_misses = 0.0;
    double after_misses = 0.0;
    uint32_t improved_ranges = 0;
    for (uint32_t range_index = 0; range_index < range_count; ++range_index) {
        const size_t range_offset = ranges_offset + size_t(range_index) * RANGE_BYTES;
        const uint32_t start = read_u32(input, range_offset);
        const uint32_t count = read_u32(input, range_offset + 4);
        std::vector<uint32_t> source(count);
        for (uint32_t index = 0; index < count; ++index) {
            source[index] = read_u32(input,
                indices_offset + size_t(start + index) * INDEX_BYTES);
        }
        const meshopt_VertexCacheStatistics before = meshopt_analyzeVertexCache(
            source.data(), count, vertex_count, CACHE_SIZE, 0, 0);
        std::vector<uint32_t> candidate(count);
        meshopt_optimizeVertexCache(candidate.data(), source.data(), count, vertex_count);
        const meshopt_VertexCacheStatistics after = meshopt_analyzeVertexCache(
            candidate.data(), count, vertex_count, CACHE_SIZE, 0, 0);
        if (!std::isfinite(before.acmr) || !std::isfinite(after.acmr)) {
            return reject("vertex-cache analysis returned a non-finite miss ratio");
        }
        before_misses += double(before.acmr) * (double(count) / 3.0);
        if (after.acmr + ACMR_EPSILON < before.acmr) {
            for (uint32_t index = 0; index < count; ++index) {
                write_u32(input, indices_offset + size_t(start + index) * INDEX_BYTES,
                    candidate[index]);
            }
            after_misses += double(after.acmr) * (double(count) / 3.0);
            ++improved_ranges;
        } else {
            after_misses += double(before.acmr) * (double(count) / 3.0);
        }
    }

    const float acmr_before = float(before_misses / (double(index_count) / 3.0));
    const float acmr_after = float(after_misses / (double(index_count) / 3.0));
    append_float(input, acmr_before);
    append_float(input, acmr_after);
    append_u32(input, improved_ranges);
    append_u32(input, range_count);
    std::cout.write(reinterpret_cast<const char*>(input.data()),
        static_cast<std::streamsize>(input.size()));
    return std::cout.good() || reject("failed to write response");
}

} // namespace

int main() {
    return run() ? 0 : 1;
}
