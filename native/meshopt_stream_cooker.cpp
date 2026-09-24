#include "meshopt_stream_codec.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <limits>
#include <vector>

namespace {

constexpr std::array<uint8_t, 8> MAGIC{{'E', 'L', 'I', 'S', 'A', 'S', 'C', '1'}};
constexpr size_t HEADER_BYTES = 16;
constexpr size_t STREAM_RECORD_BYTES = 20;
constexpr size_t MAX_PACKET_BYTES = 128u * 1024u * 1024u;
constexpr uint32_t MAX_STREAMS = 8;
constexpr uint32_t VERTEX_STREAM = 1;
constexpr uint32_t INDEX_STREAM = 2;

struct Stream {
    uint32_t kind = 0;
    uint32_t count = 0;
    uint32_t stride = 0;
    uint32_t vertex_count = 0;
    std::vector<uint8_t> source;
    std::vector<uint8_t> encoded;
};

uint32_t read_u32(const std::vector<uint8_t>& bytes, size_t offset) {
    return uint32_t(bytes[offset]) | (uint32_t(bytes[offset + 1]) << 8) |
        (uint32_t(bytes[offset + 2]) << 16) | (uint32_t(bytes[offset + 3]) << 24);
}

void append_u32(std::vector<uint8_t>& bytes, uint32_t value) {
    bytes.push_back(uint8_t(value));
    bytes.push_back(uint8_t(value >> 8));
    bytes.push_back(uint8_t(value >> 16));
    bytes.push_back(uint8_t(value >> 24));
}

bool fail(const char* message) {
    std::fprintf(stderr, "meshoptimizer stream cooker: %s\n", message);
    return false;
}

bool run() {
    std::vector<uint8_t> input;
    std::array<char, 64u * 1024u> block{};
    while (std::cin) {
        std::cin.read(block.data(), static_cast<std::streamsize>(block.size()));
        const std::streamsize read_count = std::cin.gcount();
        if (read_count <= 0) continue;
        const size_t amount = static_cast<size_t>(read_count);
        if (amount > MAX_PACKET_BYTES - input.size()) return fail("input exceeds the packet bound");
        input.insert(input.end(), block.begin(), block.begin() + read_count);
    }
    if (std::cin.bad() || input.size() < HEADER_BYTES ||
        !std::equal(MAGIC.begin(), MAGIC.end(), input.begin())) return fail("invalid packet header");
    const uint32_t stream_count = read_u32(input, 8);
    if (stream_count == 0 || stream_count > MAX_STREAMS || read_u32(input, 12) != 0) {
        return fail("invalid stream count or reserved header");
    }
    size_t offset = HEADER_BYTES;
    std::vector<Stream> streams(stream_count);
    for (Stream& stream : streams) {
        if (input.size() - offset < STREAM_RECORD_BYTES) return fail("truncated stream descriptor");
        stream.kind = read_u32(input, offset);
        stream.count = read_u32(input, offset + 4);
        stream.stride = read_u32(input, offset + 8);
        const uint32_t byte_count = read_u32(input, offset + 12);
        stream.vertex_count = read_u32(input, offset + 16);
        offset += STREAM_RECORD_BYTES;
        if ((stream.kind != VERTEX_STREAM && stream.kind != INDEX_STREAM) || stream.count == 0 ||
            stream.stride == 0 || size_t(stream.count) > std::numeric_limits<size_t>::max() / stream.stride ||
            size_t(stream.count) * stream.stride != byte_count || byte_count > input.size() - offset ||
            byte_count > MAX_PACKET_BYTES) return fail("stream dimensions exceed their bounds");
        if (stream.kind == INDEX_STREAM && stream.stride != sizeof(uint32_t)) {
            return fail("index streams must use 32-bit indices");
        }
        if (stream.kind == INDEX_STREAM && (stream.vertex_count == 0 ||
                stream.vertex_count > 2'000'000)) return fail("index vertex count is out of range");
        if (stream.kind == VERTEX_STREAM && stream.vertex_count != stream.count) {
            return fail("vertex stream count is inconsistent");
        }
        stream.source.assign(input.begin() + offset, input.begin() + offset + byte_count);
        offset += byte_count;
    }
    if (offset != input.size()) return fail("packet contains trailing bytes");

    for (Stream& stream : streams) {
        const bool encoded = stream.kind == VERTEX_STREAM
            ? elisa::assets::detail::encode_meshopt_vertex_stream(stream.source.data(),
                stream.count, stream.stride, stream.encoded)
            : elisa::assets::detail::encode_meshopt_index_stream(stream.source.data(),
                stream.count, stream.vertex_count, stream.encoded);
        if (!encoded) return fail("meshoptimizer rejected a stream");
    }

    std::vector<uint8_t> output;
    output.insert(output.end(), MAGIC.begin(), MAGIC.end());
    append_u32(output, stream_count);
    append_u32(output, 0);
    for (const Stream& stream : streams) {
        if (output.size() > MAX_PACKET_BYTES - STREAM_RECORD_BYTES ||
            stream.encoded.size() > UINT32_MAX ||
            stream.encoded.size() > MAX_PACKET_BYTES - output.size() - STREAM_RECORD_BYTES) {
            return fail("encoded streams exceed the packet bound");
        }
        append_u32(output, stream.kind);
        append_u32(output, stream.count);
        append_u32(output, stream.stride);
        append_u32(output, static_cast<uint32_t>(stream.encoded.size()));
        append_u32(output, stream.vertex_count);
        output.insert(output.end(), stream.encoded.begin(), stream.encoded.end());
    }
    std::cout.write(reinterpret_cast<const char*>(output.data()),
        static_cast<std::streamsize>(output.size()));
    return std::cout.good() || fail("failed to write encoded streams");
}

} // namespace

int main() {
    return run() ? 0 : 1;
}
