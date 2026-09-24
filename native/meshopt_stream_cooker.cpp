#include "meshopt_stream_codec.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <limits>
#include <string>
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

bool triangle_round_trip_preserves_winding(const std::vector<uint8_t>& source,
    const std::vector<uint8_t>& decoded, size_t offset) {
    const uint32_t a = read_u32(source, offset);
    const uint32_t b = read_u32(source, offset + 4);
    const uint32_t c = read_u32(source, offset + 8);
    const uint32_t x = read_u32(decoded, offset);
    const uint32_t y = read_u32(decoded, offset + 4);
    const uint32_t z = read_u32(decoded, offset + 8);
    return (x == a && y == b && z == c) || (x == b && y == c && z == a) ||
        (x == c && y == a && z == b);
}

bool index_round_trip_preserves_winding(const std::vector<uint8_t>& source,
    const std::vector<uint8_t>& decoded) {
    if (source.size() != decoded.size() || source.size() % 12 != 0) return false;
    for (size_t offset = 0; offset < source.size(); offset += 12) {
        if (!triangle_round_trip_preserves_winding(source, decoded, offset)) return false;
    }
    return true;
}

bool run(bool decode_mode) {
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
            (!decode_mode && size_t(stream.count) * stream.stride != byte_count) ||
            (decode_mode && byte_count == 0) || byte_count > input.size() - offset ||
            byte_count > MAX_PACKET_BYTES) return fail("stream dimensions exceed their bounds");
        if (stream.kind == INDEX_STREAM && stream.stride != sizeof(uint32_t)) {
            return fail("index streams must use 32-bit indices");
        }
        if (stream.kind == INDEX_STREAM && (stream.vertex_count == 0 ||
                stream.vertex_count > 2'000'000)) return fail("index vertex count is out of range");
        if (stream.kind == VERTEX_STREAM && stream.vertex_count != stream.count) {
            return fail("vertex stream count is inconsistent");
        }
        if (decode_mode) {
            stream.encoded.assign(input.begin() + offset, input.begin() + offset + byte_count);
        } else {
            stream.source.assign(input.begin() + offset, input.begin() + offset + byte_count);
        }
        offset += byte_count;
    }
    if (offset != input.size()) return fail("packet contains trailing bytes");

    for (size_t stream_index = 0; stream_index < streams.size(); ++stream_index) {
        Stream& stream = streams[stream_index];
        if (decode_mode) {
            const bool decoded = stream.kind == VERTEX_STREAM
                ? elisa::assets::detail::decode_meshopt_vertex_stream(stream.encoded.data(),
                    stream.encoded.size(), stream.count, stream.stride, stream.source)
                : elisa::assets::detail::decode_meshopt_index_stream(stream.encoded.data(),
                    stream.encoded.size(), stream.count, stream.source);
            if (!decoded) return fail("meshoptimizer rejected an encoded stream");
            continue;
        }
        const bool encoded = stream.kind == VERTEX_STREAM
            ? elisa::assets::detail::encode_meshopt_vertex_stream(stream.source.data(),
                stream.count, stream.stride, stream.encoded)
            : elisa::assets::detail::encode_meshopt_index_stream(stream.source.data(),
                stream.count, stream.vertex_count, stream.encoded);
        if (!encoded) return fail("meshoptimizer rejected a stream");
        std::vector<uint8_t> decoded;
        const bool round_trip = stream.kind == VERTEX_STREAM
            ? elisa::assets::detail::decode_meshopt_vertex_stream(stream.encoded.data(),
                stream.encoded.size(), stream.count, stream.stride, decoded)
            : elisa::assets::detail::decode_meshopt_index_stream(stream.encoded.data(),
                stream.encoded.size(), stream.count, decoded);
        const bool preserves_data = stream.kind == VERTEX_STREAM
            ? decoded == stream.source
            : index_round_trip_preserves_winding(stream.source, decoded);
        if (!round_trip || !preserves_data) {
            size_t mismatch = 0;
            if (stream.kind == VERTEX_STREAM) {
                while (mismatch < decoded.size() && mismatch < stream.source.size() &&
                    decoded[mismatch] == stream.source[mismatch]) ++mismatch;
            } else {
                for (; mismatch + 12 <= decoded.size(); mismatch += 12) {
                    if (!triangle_round_trip_preserves_winding(stream.source, decoded, mismatch)) break;
                }
            }
            std::fprintf(stderr, "meshoptimizer stream %zu kind %u round-trip mismatch at byte %zu "
                "(decoded %zu, source %zu)\n", stream_index, stream.kind, mismatch,
                decoded.size(), stream.source.size());
            return fail("meshoptimizer stream failed lossless round-trip");
        }
    }

    std::vector<uint8_t> output;
    output.insert(output.end(), MAGIC.begin(), MAGIC.end());
    append_u32(output, stream_count);
    append_u32(output, 0);
    for (const Stream& stream : streams) {
        const std::vector<uint8_t>& payload = decode_mode ? stream.source : stream.encoded;
        if (output.size() > MAX_PACKET_BYTES - STREAM_RECORD_BYTES ||
            payload.size() > UINT32_MAX ||
            payload.size() > MAX_PACKET_BYTES - output.size() - STREAM_RECORD_BYTES) {
            return fail("encoded streams exceed the packet bound");
        }
        append_u32(output, stream.kind);
        append_u32(output, stream.count);
        append_u32(output, stream.stride);
        append_u32(output, static_cast<uint32_t>(payload.size()));
        append_u32(output, stream.vertex_count);
        output.insert(output.end(), payload.begin(), payload.end());
    }
    std::cout.write(reinterpret_cast<const char*>(output.data()),
        static_cast<std::streamsize>(output.size()));
    return std::cout.good() || fail("failed to write encoded streams");
}

} // namespace

int main(int argc, char** argv) {
    const bool decode_mode = argc == 2 && std::string(argv[1]) == "--decode";
    if (argc > 2 || (argc == 2 && !decode_mode)) return 2;
    return run(decode_mode) ? 0 : 1;
}
