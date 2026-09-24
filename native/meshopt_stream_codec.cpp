#include "meshopt_stream_codec.h"

#include "meshoptimizer.h"

#include <limits>

namespace elisa::assets::detail {
namespace {

constexpr size_t MAX_STREAM_BYTES = 64u * 1024u * 1024u;
constexpr size_t MAX_VERTEX_COUNT = 2'000'000;
constexpr size_t MAX_INDEX_COUNT = 15'000'000;
constexpr size_t MAX_VERTEX_STRIDE = 256;

bool valid_vertex_stream(const uint8_t* bytes, size_t count, size_t stride) {
    return bytes != nullptr && count != 0 && count <= MAX_VERTEX_COUNT &&
        stride != 0 && stride <= MAX_VERTEX_STRIDE && count <= MAX_STREAM_BYTES / stride;
}

bool valid_index_stream(const uint8_t* bytes, size_t count) {
    return bytes != nullptr && count != 0 && count <= MAX_INDEX_COUNT && count % 3 == 0 &&
        count <= MAX_STREAM_BYTES / sizeof(uint32_t);
}

} // namespace

bool encode_meshopt_vertex_stream(const uint8_t* source, size_t vertex_count,
    size_t vertex_stride, std::vector<uint8_t>& encoded) {
    if (!valid_vertex_stream(source, vertex_count, vertex_stride)) return false;
    const size_t bound = meshopt_encodeVertexBufferBound(vertex_count, vertex_stride);
    if (bound == 0 || bound > MAX_STREAM_BYTES) return false;
    encoded.resize(bound);
    const size_t size = meshopt_encodeVertexBuffer(encoded.data(), encoded.size(), source,
        vertex_count, vertex_stride);
    if (size == 0 || size > encoded.size()) {
        encoded.clear();
        return false;
    }
    encoded.resize(size);
    return true;
}

bool decode_meshopt_vertex_stream(const uint8_t* encoded, size_t encoded_size,
    size_t vertex_count, size_t vertex_stride, std::vector<uint8_t>& decoded) {
    if (encoded == nullptr || encoded_size == 0 || encoded_size > MAX_STREAM_BYTES ||
        !valid_vertex_stream(encoded, vertex_count, vertex_stride)) return false;
    decoded.resize(vertex_count * vertex_stride);
    if (meshopt_decodeVertexBuffer(decoded.data(), vertex_count, vertex_stride,
            encoded, encoded_size) != 0) {
        decoded.clear();
        return false;
    }
    return true;
}

bool encode_meshopt_index_stream(const uint8_t* source, size_t index_count,
    size_t vertex_count, std::vector<uint8_t>& encoded) {
    if (!valid_index_stream(source, index_count) || vertex_count == 0 ||
        vertex_count > MAX_VERTEX_COUNT) return false;
    std::vector<uint32_t> indices(index_count);
    for (size_t index = 0; index < index_count; ++index) {
        const size_t offset = index * sizeof(uint32_t);
        indices[index] = uint32_t(source[offset]) | (uint32_t(source[offset + 1]) << 8) |
            (uint32_t(source[offset + 2]) << 16) | (uint32_t(source[offset + 3]) << 24);
        if (indices[index] >= vertex_count) return false;
    }
    const size_t bound = meshopt_encodeIndexBufferBound(index_count, vertex_count);
    if (bound == 0 || bound > MAX_STREAM_BYTES) return false;
    encoded.resize(bound);
    const size_t size = meshopt_encodeIndexBuffer(encoded.data(), encoded.size(),
        indices.data(), index_count);
    if (size == 0 || size > encoded.size()) {
        encoded.clear();
        return false;
    }
    encoded.resize(size);
    return true;
}

bool decode_meshopt_index_stream(const uint8_t* encoded, size_t encoded_size,
    size_t index_count, std::vector<uint8_t>& decoded) {
    if (encoded == nullptr || encoded_size == 0 || encoded_size > MAX_STREAM_BYTES ||
        !valid_index_stream(encoded, index_count)) return false;
    std::vector<uint32_t> indices(index_count);
    if (meshopt_decodeIndexBuffer(indices.data(), index_count, sizeof(uint32_t),
            encoded, encoded_size) != 0) return false;
    decoded.resize(index_count * sizeof(uint32_t));
    for (size_t index = 0; index < index_count; ++index) {
        const size_t offset = index * sizeof(uint32_t);
        const uint32_t value = indices[index];
        decoded[offset] = uint8_t(value);
        decoded[offset + 1] = uint8_t(value >> 8);
        decoded[offset + 2] = uint8_t(value >> 16);
        decoded[offset + 3] = uint8_t(value >> 24);
    }
    return true;
}

} // namespace elisa::assets::detail
