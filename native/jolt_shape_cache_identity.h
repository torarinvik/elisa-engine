#pragma once

#include "cooked_collision_geometry.h"
#include "cooked_geometry_package.h"
#include "jolt_shape_cache.h"
#include "sha256_file.h"

#include <cstdint>
#include <filesystem>
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

} // namespace elisa::physics::shape_cache
