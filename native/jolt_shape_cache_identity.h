#pragma once

#include "cooked_collision_geometry.h"
#include "cooked_geometry_package.h"
#include "jolt_shape_cache.h"
#include "sha256_file.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>

namespace elisa::physics::shape_cache {

struct Identity {
    std::filesystem::path path;
    std::string key;
    bool enabled = false;
};

inline Identity make_identity(const std::filesystem::path& asset_path, int32_t kind,
        float scale_x, float scale_y, float scale_z,
        const ElisaCoordinateProfile& profile, uint64_t jolt_version) noexcept {
    Identity identity;
    try {
        std::error_code error;
        const uint64_t source_size = std::filesystem::file_size(asset_path, error);
        std::string source_digest, digest_error;
        std::filesystem::path project_root;
        if (!error && source_size > 0 &&
            source_size <= assets::MAX_COLLISION_PACKAGE_BYTES &&
            assets::sha256_file(asset_path, source_size, source_digest, digest_error) &&
            assets::project_asset_root(project_root) &&
            make_cache_path(project_root, asset_path, kind, scale_x, scale_y,
                scale_z, profile, identity.path)) {
            identity.enabled = make_cache_key(source_digest, kind, scale_x,
                scale_y, scale_z, profile, jolt_version, identity.key);
        }
    } catch (...) {
        identity.enabled = false;
    }
    return identity;
}

inline bool make_array_geometry_digest(const float* positions, size_t vertex_count,
        const uint32_t* indices, size_t index_count, std::string& result) {
    if (positions == nullptr || indices == nullptr || vertex_count == 0 || index_count == 0 ||
        vertex_count > std::numeric_limits<size_t>::max() / 3) {
        return false;
    }
    constexpr char domain[] = "elisa-jolt-shape-array-geometry-v1";
    assets::Sha256 hash;
    hash.update(reinterpret_cast<const uint8_t*>(domain), sizeof(domain) - 1);
    std::array<uint8_t, sizeof(uint64_t)> count{};
    write_u64(count.data(), vertex_count);
    hash.update(count.data(), count.size());
    for (size_t index = 0; index < vertex_count * 3; ++index) {
        uint32_t bits = 0;
        std::memcpy(&bits, positions + index, sizeof(bits));
        std::array<uint8_t, sizeof(uint32_t)> encoded{};
        write_u32(encoded.data(), bits);
        hash.update(encoded.data(), encoded.size());
    }
    write_u64(count.data(), index_count);
    hash.update(count.data(), count.size());
    for (size_t index = 0; index < index_count; ++index) {
        std::array<uint8_t, sizeof(uint32_t)> encoded{};
        write_u32(encoded.data(), indices[index]);
        hash.update(encoded.data(), encoded.size());
    }
    result = hash.finish();
    return valid_digest(result);
}

inline bool make_array_geometry_identity(const std::filesystem::path& project_root,
        int32_t kind, const float* positions, size_t vertex_count,
        const uint32_t* indices, size_t index_count,
        const ElisaCoordinateProfile& profile, uint64_t jolt_version,
        std::filesystem::path& path, std::string& key) {
    std::string geometry_digest;
    if (!make_array_geometry_digest(positions, vertex_count, indices, index_count,
            geometry_digest) || !make_cache_key(geometry_digest, kind,
            1.0f, 1.0f, 1.0f, profile, jolt_version, key)) return false;
    path = project_root / "build" / "cache" / "physics" / (key + ".joltshape");
    return true;
}

} // namespace elisa::physics::shape_cache
