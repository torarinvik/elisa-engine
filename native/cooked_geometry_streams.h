#pragma once

// Decode raw and losslessly meshoptimizer-coded vertex/index package streams.
#include "cooked_package_fields.h"
#include "meshopt_stream_codec.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace elisa::assets::detail {

inline bool decode_geometry_vertex_bytes(const probe::PackageIndex& package, const char* field,
    size_t vertex_count, size_t vertex_stride, std::vector<uint8_t>& bytes) {
    const std::string raw_key = std::string(field) + "_b64";
    const std::string meshopt_key = std::string(field) + "_meshopt_b64";
    const auto raw = package.sections.find(raw_key);
    const auto encoded = package.sections.find(meshopt_key);
    if ((raw == package.sections.end()) == (encoded == package.sections.end()) ||
        vertex_count == 0 || vertex_stride == 0 ||
        vertex_count > 16u * 1024u * 1024u / vertex_stride) return false;
    const size_t expected_size = vertex_count * vertex_stride;
    if (raw != package.sections.end()) {
        return decode_base64(raw->second, bytes) && bytes.size() == expected_size;
    }
    std::vector<uint8_t> compressed;
    return decode_base64(encoded->second, compressed) &&
        decode_meshopt_vertex_stream(compressed.data(), compressed.size(), vertex_count,
            vertex_stride, bytes) && bytes.size() == expected_size;
}

inline bool decode_geometry_floats(const probe::PackageIndex& package, const char* field,
    size_t vertex_count, size_t vertex_stride, std::vector<float>& output) {
    std::vector<uint8_t> bytes;
    if (vertex_stride % sizeof(float) != 0 ||
        !decode_geometry_vertex_bytes(package, field, vertex_count, vertex_stride, bytes)) return false;
    output.resize(bytes.size() / sizeof(float));
    for (size_t index = 0; index < output.size(); ++index) {
        const size_t offset = index * sizeof(float);
        const uint32_t bits = uint32_t(bytes[offset]) | (uint32_t(bytes[offset + 1]) << 8) |
            (uint32_t(bytes[offset + 2]) << 16) | (uint32_t(bytes[offset + 3]) << 24);
        std::memcpy(&output[index], &bits, sizeof(bits));
        if (!std::isfinite(output[index])) return false;
    }
    return true;
}

inline bool decode_geometry_u32(const probe::PackageIndex& package, const char* field,
    size_t vertex_count, size_t values_per_vertex, std::vector<uint32_t>& output) {
    if (values_per_vertex == 0 || values_per_vertex > std::numeric_limits<size_t>::max() / 4) return false;
    std::vector<uint8_t> bytes;
    if (!decode_geometry_vertex_bytes(package, field, vertex_count, values_per_vertex * 4, bytes)) return false;
    output.resize(bytes.size() / 4);
    for (size_t index = 0; index < output.size(); ++index) {
        const size_t offset = index * 4;
        output[index] = uint32_t(bytes[offset]) | (uint32_t(bytes[offset + 1]) << 8) |
            (uint32_t(bytes[offset + 2]) << 16) | (uint32_t(bytes[offset + 3]) << 24);
    }
    return true;
}

inline bool decode_geometry_index_bytes(const probe::PackageIndex& package, size_t index_count,
    std::vector<uint8_t>& bytes) {
    const auto raw = package.sections.find("indices_b64");
    const auto encoded = package.sections.find("indices_meshopt_b64");
    if ((raw == package.sections.end()) == (encoded == package.sections.end()) || index_count == 0 ||
        index_count > std::numeric_limits<size_t>::max() / sizeof(uint32_t)) return false;
    const size_t expected_size = index_count * sizeof(uint32_t);
    if (raw != package.sections.end()) {
        return decode_base64(raw->second, bytes) && bytes.size() == expected_size;
    }
    std::vector<uint8_t> compressed;
    return decode_base64(encoded->second, compressed) &&
        decode_meshopt_index_stream(compressed.data(), compressed.size(), index_count, bytes) &&
        bytes.size() == expected_size;
}

} // namespace elisa::assets::detail
