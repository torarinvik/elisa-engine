#pragma once

// Lossless meshoptimizer codecs for bounded cooked geometry vertex and index
// streams. The cook tool and runtime share these exact wrappers.
#include <cstddef>
#include <cstdint>
#include <vector>

namespace elisa::assets::detail {

bool encode_meshopt_vertex_stream(const uint8_t* source, size_t vertex_count,
    size_t vertex_stride, std::vector<uint8_t>& encoded);
bool decode_meshopt_vertex_stream(const uint8_t* encoded, size_t encoded_size,
    size_t vertex_count, size_t vertex_stride, std::vector<uint8_t>& decoded);
bool encode_meshopt_index_stream(const uint8_t* source, size_t index_count,
    size_t vertex_count, std::vector<uint8_t>& encoded);
bool decode_meshopt_index_stream(const uint8_t* encoded, size_t encoded_size,
    size_t index_count, std::vector<uint8_t>& decoded);

} // namespace elisa::assets::detail
