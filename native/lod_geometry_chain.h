#pragma once

// Resolves and validates every package in a static LOD chain before runtime use.
#include "bundle_dependencies.h"
#include "cooked_geometry_package.h"
#include "lod_manifest.h"
#include "sha256_file.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>

namespace elisa::assets {

inline constexpr size_t MAX_LOD_CHAIN_PACKAGE_BYTES = 256u * 1024u * 1024u;
inline constexpr size_t MAX_LOD_CHAIN_RESIDENT_BYTES = 256u * 1024u * 1024u;

struct LodGeometryChain {
    LodManifest manifest;
    std::array<CookedGeometry, MAX_LOD_LEVELS> geometry{};
    std::array<std::filesystem::path, MAX_LOD_LEVELS> package_paths{};
    size_t level_count = 0;
    size_t package_bytes = 0;
    size_t resident_bytes = 0;
    bool valid = false;
    std::string error;
};

namespace lod_chain_detail {

inline bool add_bytes(size_t count, size_t stride, size_t& total) {
    if (stride != 0 && count > (MAX_LOD_CHAIN_RESIDENT_BYTES - total) / stride) return false;
    total += count * stride;
    return true;
}

inline bool geometry_bytes(const CookedGeometry& geometry, size_t& bytes) {
    size_t total = 0;
    if (!add_bytes(geometry.positions.size(), sizeof(float), total) ||
        !add_bytes(geometry.normals.size(), sizeof(float), total) ||
        !add_bytes(geometry.uvs.size(), sizeof(float), total) ||
        !add_bytes(geometry.tangents.size(), sizeof(float), total) ||
        !add_bytes(geometry.indices.size(), sizeof(uint32_t), total) ||
        !add_bytes(geometry.subsets.size(), sizeof(CookedGeometry::Subset), total) ||
        !add_bytes(geometry.mesh_placements.size(), sizeof(CookedGeometry::MeshPlacement), total) ||
        !add_bytes(geometry.slot_materials.size(), sizeof(CookedSlotMaterial), total) ||
        !add_bytes(geometry.texture_sections.size(), sizeof(std::string), total) ||
        !add_bytes(geometry.texture_checksums.size(), sizeof(uint32_t), total) ||
        !add_bytes(geometry.cameras.size(), sizeof(CookedGeometry::Camera), total) ||
        !add_bytes(geometry.lights.size(), sizeof(CookedGeometry::Light), total)) return false;
    for (const std::string& name : geometry.texture_sections) {
        if (!add_bytes(name.size(), 1, total)) return false;
    }
    bytes = total;
    return true;
}

inline bool static_geometry(const CookedGeometry& geometry) {
    return geometry.skin_bone_names.empty() && geometry.skin_indices.empty() &&
        geometry.skin_weights.empty() && geometry.skin_inverse_bind_matrices.empty() &&
        geometry.skin_cluster_joints.empty() && geometry.skin_joints.empty() &&
        geometry.animation_clips.empty() && geometry.morph_targets.empty() &&
        geometry.morph_default_weights.empty();
}

inline bool same_material(const CookedSlotMaterial& left, const CookedSlotMaterial& right) {
    return left.base_color == right.base_color && left.metallic == right.metallic &&
        left.roughness == right.roughness && left.emissive == right.emissive &&
        left.alpha_cutoff == right.alpha_cutoff && left.alpha_mode == right.alpha_mode &&
        left.double_sided == right.double_sided && left.occlusion == right.occlusion &&
        left.textures == right.textures;
}

inline bool same_layout(const CookedGeometry& full, const CookedGeometry& lod) {
    if (full.material_slots != lod.material_slots || full.mesh_count != lod.mesh_count ||
        full.subsets.size() != lod.subsets.size() || full.mesh_placements.size() != lod.mesh_placements.size() ||
        full.slot_materials.size() != lod.slot_materials.size() ||
        full.texture_sections != lod.texture_sections || full.texture_checksums != lod.texture_checksums ||
        full.cameras.size() != lod.cameras.size() || full.lights.size() != lod.lights.size()) return false;
    for (size_t index = 0; index < full.subsets.size(); ++index) {
        if (full.subsets[index].material_slot != lod.subsets[index].material_slot) return false;
    }
    for (size_t index = 0; index < full.slot_materials.size(); ++index) {
        if (!same_material(full.slot_materials[index], lod.slot_materials[index])) return false;
    }
    for (size_t index = 0; index < full.mesh_placements.size(); ++index) {
        const auto& left = full.mesh_placements[index];
        const auto& right = lod.mesh_placements[index];
        if (left.mesh != right.mesh || left.node != right.node || left.subset_count != right.subset_count ||
            left.transform != right.transform) return false;
    }
    for (size_t index = 0; index < full.cameras.size(); ++index) {
        const auto& left = full.cameras[index];
        const auto& right = lod.cameras[index];
        if (left.node != right.node || left.projection != right.projection || left.x != right.x ||
            left.y != right.y || left.near_clip != right.near_clip || left.far_clip != right.far_clip ||
            left.transform != right.transform) return false;
    }
    for (size_t index = 0; index < full.lights.size(); ++index) {
        const auto& left = full.lights[index];
        const auto& right = lod.lights[index];
        if (left.node != right.node || left.kind != right.kind || left.intensity != right.intensity ||
            left.range != right.range || left.inner_cone != right.inner_cone ||
            left.outer_cone != right.outer_cone || left.color != right.color ||
            left.transform != right.transform) return false;
    }
    return true;
}

inline bool package_path(const std::filesystem::path& root, const std::filesystem::path& manifest_path,
        const std::string& relative_package, std::filesystem::path& resolved) {
    const std::filesystem::path logical_manifest = manifest_path.lexically_relative(root);
    const std::filesystem::path logical_package = logical_manifest.parent_path() /
        std::filesystem::u8path(relative_package);
    const std::string path = logical_package.generic_string();
    return resolve_project_asset_path(path.c_str(), resolved);
}

} // namespace lod_chain_detail

inline bool load_lod_geometry_chain(const std::string& manifest_asset_path, LodGeometryChain& chain,
        probe::BinaryPackageReadCancellationCheck cancellation_check = {}) {
    chain = {};
    std::filesystem::path manifest_path;
    if (!resolve_project_asset_path(manifest_asset_path.c_str(), manifest_path)) {
        chain.error = "LOD manifest path is outside the project asset root";
        return false;
    }
    std::filesystem::path root;
    if (!project_asset_root(root)) {
        chain.error = "project asset root is unavailable";
        return false;
    }
    chain.manifest = read_lod_manifest(manifest_path.string());
    if (!chain.manifest.valid) {
        chain.error = chain.manifest.error;
        return false;
    }
    for (size_t index = 0; index < chain.manifest.level_count; ++index) {
        const LodManifestLevel& level = chain.manifest.levels[index];
        std::filesystem::path package_path;
        if (!lod_chain_detail::package_path(root, manifest_path, level.package, package_path)) {
            chain.error = "LOD package path escaped the project asset root";
            return false;
        }
        if (level.byte_size > MAX_LOD_CHAIN_PACKAGE_BYTES - chain.package_bytes) {
            chain.error = "LOD chain package byte budget exceeded";
            return false;
        }
        std::string digest;
        if (!sha256_file(package_path, level.byte_size, digest, chain.error, cancellation_check) ||
            digest != level.sha256) {
            if (chain.error.empty()) chain.error = "LOD package SHA-256 mismatch";
            return false;
        }
        if (!verify_bundle_dependencies(package_path, chain.error) ||
            !load_cooked_geometry_asset(package_path.string(), chain.geometry[index], chain.error,
                cancellation_check)) return false;
        const CookedGeometry& geometry = chain.geometry[index];
        if (!lod_chain_detail::static_geometry(geometry) || geometry.indices.size() / 3 != level.triangles ||
            geometry.positions.size() / 3 != level.vertices ||
            (index != 0 && !lod_chain_detail::same_layout(chain.geometry[0], geometry))) {
            chain.error = "LOD package geometry or material layout mismatch";
            return false;
        }
        size_t resident = 0;
        if (!lod_chain_detail::geometry_bytes(geometry, resident) ||
            resident > MAX_LOD_CHAIN_RESIDENT_BYTES - chain.resident_bytes) {
            chain.error = "LOD chain resident byte budget exceeded";
            return false;
        }
        chain.package_bytes += static_cast<size_t>(level.byte_size);
        chain.resident_bytes += resident;
        chain.package_paths[index] = std::move(package_path);
    }
    chain.level_count = chain.manifest.level_count;
    chain.valid = true;
    return true;
}

} // namespace elisa::assets
