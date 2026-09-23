#pragma once

#include "fbx_asset_import_types.h"
#include "ufbx.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace elisa::assets::detail {

constexpr uint64_t MAX_FILE_BYTES = 512ull * 1024ull * 1024ull;
// FBX triangle-corner expansion can exceed the compact source size by many
// times; these are hard ceilings for the offline cooker, not eager allocations.
constexpr size_t MAX_TEMP_MEMORY_BYTES = size_t(1536) * 1024 * 1024;
constexpr size_t MAX_SCENE_MEMORY_BYTES = size_t(3072) * 1024 * 1024;
constexpr size_t MAX_INDEX_MEMORY_BYTES = size_t(768) * 1024 * 1024;
constexpr size_t MAX_SCENE_NODES = 4096;
constexpr size_t MAX_SCENE_MESHES = 1024;
constexpr size_t MAX_SCENE_BONES = 4096;
constexpr size_t MAX_SCENE_MATERIALS = 1024;
constexpr size_t MAX_ANIMATION_STACKS = 256;
constexpr size_t MAX_SKIN_JOINTS = 64;
constexpr size_t MAX_ANIMATION_CLIPS = 24;
constexpr uint32_t ANIMATION_SAMPLE_RATE = 30;
constexpr size_t MAX_ANIMATION_SAMPLE_FLOATS = 2'000'000;
constexpr size_t MAX_MESH_TRIANGLES = 5'000'000;
constexpr size_t MAX_EXTRACTED_BYTES = size_t(1024) * 1024 * 1024;

inline void fail(FbxImportResult& result, const std::string& message) {
    result.error = message;
    result.ok = false;
}

inline std::string ufbx_error_text(const ufbx_error& error) {
    if (error.description.data == nullptr || error.description.length == 0) {
        return "ufbx could not load the FBX scene";
    }
    constexpr size_t MAX_ERROR_LENGTH = 4096;
    const size_t length = std::min(error.description.length, MAX_ERROR_LENGTH);
    return std::string(error.description.data, length);
}

inline bool finite(ufbx_vec3 value) {
    return std::isfinite(double(value.x)) && std::isfinite(double(value.y)) &&
        std::isfinite(double(value.z));
}

inline bool finite(ufbx_quat value) {
    return std::isfinite(double(value.x)) && std::isfinite(double(value.y)) &&
        std::isfinite(double(value.z)) && std::isfinite(double(value.w));
}

} // namespace elisa::assets::detail
