#pragma once

// Whitelist lossless vertex-stream names accepted by cooked geometry.
#include <cstddef>
#include <cstdint>
#include <string>

namespace elisa::assets {

inline constexpr uint32_t MAX_GEOMETRY_MORPH_TARGETS = 32;

namespace detail {

inline bool morph_meshopt_stream_field(const std::string& name, size_t& target, bool& normal) {
    if (name.compare(0, 6, "morph_") != 0) return false;
    const size_t separator = name.find('_', 6);
    if (separator == std::string::npos || separator == 6) return false;
    target = 0;
    for (size_t index = 6; index < separator; ++index) {
        const char digit = name[index];
        if (digit < '0' || digit > '9' || target > MAX_GEOMETRY_MORPH_TARGETS) return false;
        target = target * 10 + size_t(digit - '0');
    }
    if (target >= MAX_GEOMETRY_MORPH_TARGETS) return false;
    const std::string suffix = name.substr(separator);
    if (suffix == "_positions_meshopt_b64") {
        normal = false;
        return true;
    }
    if (suffix == "_normals_meshopt_b64") {
        normal = true;
        return true;
    }
    return false;
}

inline bool supported_meshopt_stream_field(const std::string& name) {
    if (name == "positions_meshopt_b64" || name == "normals_meshopt_b64" ||
        name == "uvs_meshopt_b64" || name == "tangents_meshopt_b64" ||
        name == "uv1s_meshopt_b64" || name == "indices_meshopt_b64" ||
        name == "skin_indices_meshopt_b64" || name == "skin_weights_meshopt_b64") return true;
    size_t target = 0;
    bool normal = false;
    return morph_meshopt_stream_field(name, target, normal);
}

} // namespace detail
} // namespace elisa::assets
