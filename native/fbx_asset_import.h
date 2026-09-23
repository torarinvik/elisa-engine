#pragma once

// Bounded FBX parsing and mesh normalization for engine-owned cookers.
// ufbx is fetched and hash-pinned by scripts/fetch_dependencies.py; no FBX or
// Wicked types escape the engine's native import boundary.
#include "fbx_asset_import_mesh.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

namespace elisa::assets {

// Parse one FBX into bounded scene metadata and optionally decode its largest
// triangle mesh, select one exact node/mesh name, or combine every static
// triangle mesh into one cooked geometry stream. The default avoids helper
// geometry unexpectedly becoming the cooked asset; all-mesh mode is explicit
// until node-preserving scene packages are available.
// Axis and unit conversion happen in ufbx at the import boundary: output
// positions use metres in Elisa's right-handed +Y-up frame.
inline FbxImportResult import_fbx(const std::filesystem::path& path,
    bool decode_first_mesh = true, const std::string& selected_mesh_name = {},
    bool ignore_material_textures = false, bool all_meshes = false) {
    FbxImportResult result;
    if (all_meshes && !selected_mesh_name.empty()) {
        detail::fail(result, "--all-meshes cannot be combined with an exact mesh selector");
        return result;
    }
    if (selected_mesh_name.size() > 512 || selected_mesh_name.find('\0') != std::string::npos ||
        selected_mesh_name.find_first_of("\r\n") != std::string::npos) {
        detail::fail(result, "FBX mesh selector must be at most 512 bytes without line breaks");
        return result;
    }
    std::error_code file_error;
    const uintmax_t file_bytes = std::filesystem::file_size(path, file_error);
    if (file_error || file_bytes == 0 || file_bytes > detail::MAX_FILE_BYTES) {
        detail::fail(result, file_error ? "FBX source is not a readable regular file" :
            (file_bytes == 0 ? "FBX source is empty" : "FBX source exceeds the file-size limit"));
        return result;
    }
    result.file_bytes = uint64_t(file_bytes);

    ufbx_load_opts options{};
    options.strict = true;
    options.generate_missing_normals = true;
    options.use_blender_pbr_material = true;
    options.ignore_embedded = true;
    options.load_external_files = false;
    options.node_depth_limit = 256;
    options.target_axes = ufbx_axes_right_handed_y_up;
    options.target_unit_meters = 1.0;
    options.temp_allocator.memory_limit = detail::MAX_TEMP_MEMORY_BYTES;
    options.result_allocator.memory_limit = detail::MAX_SCENE_MEMORY_BYTES;
    ufbx_error error{};
    const std::string filename = path.string();
    ufbx_scene* loaded = ufbx_load_file(filename.c_str(), &options, &error);
    if (loaded == nullptr) {
        detail::fail(result, "FBX scene loading failed (ufbx error " +
            std::to_string(int(error.type)) + "): " + detail::ufbx_error_text(error));
        return result;
    }
    std::unique_ptr<ufbx_scene, decltype(&ufbx_free_scene)> scene(loaded, &ufbx_free_scene);
    result.nodes = scene->nodes.count;
    result.meshes = scene->meshes.count;
    result.bones = scene->bones.count;
    result.skins = scene->skin_deformers.count;
    result.materials = scene->materials.count;
    result.animation_stacks = scene->anim_stacks.count;
    result.source_unit_meters = double(scene->settings.unit_meters);
    result.frames_per_second = scene->settings.frames_per_second;
    if (result.nodes > detail::MAX_SCENE_NODES || result.meshes > detail::MAX_SCENE_MESHES ||
        result.bones > detail::MAX_SCENE_BONES || result.materials > detail::MAX_SCENE_MATERIALS ||
        result.animation_stacks > detail::MAX_ANIMATION_STACKS) {
        detail::fail(result, "FBX scene exceeds a bounded element-count limit");
        return result;
    }
    for (size_t index = 0; index < scene->bones.count; ++index) {
        const ufbx_bone* bone = scene->bones.data[index];
        if (bone == nullptr) continue;
        for (size_t character = 0; character < bone->name.length; ++character) {
            result.bone_name_hash ^= uint8_t(bone->name.data[character]);
            result.bone_name_hash *= 1099511628211ull;
        }
        result.bone_name_hash ^= 0xffu;
        result.bone_name_hash *= 1099511628211ull;
    }
    for (size_t index = 0; index < scene->materials.count; ++index) {
        const ufbx_material* material = scene->materials.data[index];
        if (material != nullptr) result.texture_references += material->textures.count;
    }
    for (size_t index = 0; index < scene->meshes.count; ++index) {
        const ufbx_mesh* mesh = scene->meshes.data[index];
        if (mesh == nullptr) continue;
        if (mesh->num_triangles > detail::MAX_MESH_TRIANGLES ||
            result.triangles > detail::MAX_MESH_TRIANGLES - mesh->num_triangles) {
            detail::fail(result, "FBX scene exceeds the bounded triangle limit");
            return result;
        }
        result.triangles += mesh->num_triangles;
        result.vertices += mesh->num_vertices;
    }
    if (scene->anim_stacks.count > 0) {
        const ufbx_anim_stack* clip = scene->anim_stacks.data[0];
        if (clip != nullptr) {
            result.first_clip_name.assign(clip->name.data ? clip->name.data : "", clip->name.length);
            result.first_clip_duration_seconds = std::max(0.0,
                double(clip->time_end) - double(clip->time_begin));
        }
    }
    if (decode_first_mesh &&
        !detail::extract_scene_meshes(*scene, result, selected_mesh_name, all_meshes,
            ignore_material_textures)) return result;
    result.ok = true;
    return result;
}

} // namespace elisa::assets
