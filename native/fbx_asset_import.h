#pragma once

// Bounded FBX parsing and first-mesh normalization for engine-owned cookers.
// ufbx is fetched and hash-pinned by scripts/fetch_dependencies.py; no FBX or
// Wicked types escape the engine's native import boundary.
#include "ufbx.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace elisa::assets {

struct FbxVertex {
    float position[3]{};
    float normal[3]{};
    float uv[2]{};
};
static_assert(sizeof(FbxVertex) == sizeof(float) * 8);

struct FbxMeshData {
    std::string node_name;
    std::string mesh_name;
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> uvs;
    std::vector<uint32_t> indices;
    float bounds_min[3] = {
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(),
    };
    float bounds_max[3] = {
        -std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
    };
};

struct FbxImportResult {
    std::string error;
    uint64_t file_bytes = 0;
    uint64_t nodes = 0;
    uint64_t meshes = 0;
    uint64_t vertices = 0;
    uint64_t triangles = 0;
    uint64_t bones = 0;
    uint64_t bone_name_hash = 14695981039346656037ull;
    uint64_t skins = 0;
    uint64_t materials = 0;
    uint64_t texture_references = 0;
    uint64_t animation_stacks = 0;
    double source_unit_meters = 0.0;
    double frames_per_second = 0.0;
    double first_clip_duration_seconds = 0.0;
    std::string first_clip_name;
    FbxMeshData primary_mesh;
    bool primary_mesh_extracted = false;
    bool ok = false;
};

namespace detail {

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

inline ufbx_vec3 face_normal(ufbx_vec3 a, ufbx_vec3 b, ufbx_vec3 c) {
    const double abx = double(b.x) - a.x;
    const double aby = double(b.y) - a.y;
    const double abz = double(b.z) - a.z;
    const double acx = double(c.x) - a.x;
    const double acy = double(c.y) - a.y;
    const double acz = double(c.z) - a.z;
    const double x = aby * acz - abz * acy;
    const double y = abz * acx - abx * acz;
    const double z = abx * acy - aby * acx;
    const double length = std::sqrt(x * x + y * y + z * z);
    if (!(length > 1.0e-20) || !std::isfinite(length)) return ufbx_zero_vec3;
    return ufbx_vec3{ufbx_real(x / length), ufbx_real(y / length), ufbx_real(z / length)};
}

inline bool extract_primary_mesh(ufbx_scene& scene, FbxImportResult& result) {
    ufbx_node* source_node = nullptr;
    for (size_t index = 0; index < scene.nodes.count; ++index) {
        ufbx_node* node = scene.nodes.data[index];
        if (node != nullptr && node->mesh != nullptr && node->mesh->num_triangles != 0 &&
            (source_node == nullptr || node->mesh->num_triangles > source_node->mesh->num_triangles)) {
            source_node = node;
        }
    }
    if (source_node == nullptr) {
        fail(result, "FBX scene has no triangle mesh node");
        return false;
    }

    ufbx_mesh& mesh = *source_node->mesh;
    if (!mesh.vertex_position.exists || mesh.num_triangles > MAX_MESH_TRIANGLES || mesh.max_face_triangles == 0 ||
        mesh.max_face_triangles > MAX_MESH_TRIANGLES) {
        fail(result, "FBX mesh exceeds the bounded triangle limit");
        return false;
    }
    if (mesh.num_triangles > std::numeric_limits<size_t>::max() / 3) {
        fail(result, "FBX mesh triangle count overflows the output index range");
        return false;
    }
    const size_t corner_count = mesh.num_triangles * 3;
    if (corner_count > MAX_EXTRACTED_BYTES / sizeof(FbxVertex) ||
        corner_count > std::numeric_limits<uint32_t>::max()) {
        fail(result, "FBX mesh exceeds the bounded extracted-geometry budget");
        return false;
    }

    FbxMeshData output;
    output.node_name.assign(source_node->name.data ? source_node->name.data : "",
        source_node->name.length);
    output.mesh_name.assign(mesh.name.data ? mesh.name.data : "", mesh.name.length);
    std::vector<FbxVertex> corners;
    std::vector<uint32_t> indices(corner_count);
    corners.reserve(corner_count);
    std::vector<uint32_t> triangle_indices(mesh.max_face_triangles * 3);
    const ufbx_matrix normal_matrix = ufbx_matrix_for_normals(&source_node->geometry_to_world);

    for (size_t face_index = 0; face_index < mesh.faces.count; ++face_index) {
        const ufbx_face face = mesh.faces.data[face_index];
        if (face.num_indices < 3) continue;
        const size_t triangle_count = ufbx_triangulate_face(
            triangle_indices.data(), triangle_indices.size(), &mesh, face);
        if (triangle_count == 0 || triangle_count > mesh.max_face_triangles) {
            fail(result, "ufbx could not triangulate an FBX mesh face");
            return false;
        }
        for (size_t corner = 0; corner < triangle_count * 3; ++corner) {
            const uint32_t index = triangle_indices[corner];
            if (index >= mesh.vertex_position.indices.count) {
                fail(result, "FBX triangulation returned an invalid vertex index");
                return false;
            }
            const ufbx_vec3 source_position = ufbx_get_vertex_vec3(&mesh.vertex_position, index);
            if (!finite(source_position)) {
                fail(result, "FBX mesh contains a non-finite position");
                return false;
            }
            const ufbx_vec3 position = ufbx_transform_position(&source_node->geometry_to_world, source_position);
            if (!finite(position)) {
                fail(result, "FBX transform produced a non-finite position");
                return false;
            }
            FbxVertex vertex;
            vertex.position[0] = float(position.x);
            vertex.position[1] = float(position.y);
            vertex.position[2] = float(position.z);
            if (!std::isfinite(vertex.position[0]) || !std::isfinite(vertex.position[1]) ||
                !std::isfinite(vertex.position[2])) {
                fail(result, "FBX position exceeds normalized float range");
                return false;
            }
            if (mesh.vertex_normal.exists && index < mesh.vertex_normal.indices.count) {
                const ufbx_vec3 source_normal = ufbx_get_vertex_vec3(&mesh.vertex_normal, index);
                if (!finite(source_normal)) {
                    fail(result, "FBX mesh contains a non-finite normal");
                    return false;
                }
                const ufbx_vec3 normal = ufbx_vec3_normalize(
                    ufbx_transform_direction(&normal_matrix, source_normal));
                if (!finite(normal)) {
                    fail(result, "FBX normal transform produced a non-finite value");
                    return false;
                }
                vertex.normal[0] = float(normal.x);
                vertex.normal[1] = float(normal.y);
                vertex.normal[2] = float(normal.z);
                if (!std::isfinite(vertex.normal[0]) || !std::isfinite(vertex.normal[1]) ||
                    !std::isfinite(vertex.normal[2])) {
                    fail(result, "FBX normal exceeds normalized float range");
                    return false;
                }
            }
            if (mesh.vertex_uv.exists && index < mesh.vertex_uv.indices.count) {
                const ufbx_vec2 uv = ufbx_get_vertex_vec2(&mesh.vertex_uv, index);
                if (!std::isfinite(double(uv.x)) || !std::isfinite(double(uv.y))) {
                    fail(result, "FBX mesh contains a non-finite UV");
                    return false;
                }
                vertex.uv[0] = float(uv.x);
                vertex.uv[1] = float(uv.y);
                if (!std::isfinite(vertex.uv[0]) || !std::isfinite(vertex.uv[1])) {
                    fail(result, "FBX UV exceeds normalized float range");
                    return false;
                }
            }
            corners.push_back(vertex);
        }
    }
    if (corners.size() != corner_count) {
        fail(result, "FBX face triangulation disagrees with the reported triangle count");
        return false;
    }

    for (size_t index = 0; index < corner_count; ++index) indices[index] = uint32_t(index);
    ufbx_vertex_stream stream{corners.data(), corners.size(), sizeof(FbxVertex)};
    ufbx_allocator_opts index_allocator{};
    index_allocator.memory_limit = MAX_INDEX_MEMORY_BYTES;
    ufbx_error index_error{};
    const size_t unique_count = ufbx_generate_indices(&stream, 1, indices.data(),
        corner_count, &index_allocator, &index_error);
    if (unique_count == 0 || unique_count > corners.size() || index_error.type != UFBX_ERROR_NONE) {
        fail(result, index_error.type == UFBX_ERROR_NONE
            ? "FBX vertex indexing produced no vertices" : ufbx_error_text(index_error));
        return false;
    }
    corners.resize(unique_count);
    indices.resize(corner_count);
    output.positions.reserve(unique_count * 3);
    output.normals.reserve(unique_count * 3);
    output.uvs.reserve(unique_count * 2);
    for (const FbxVertex& vertex : corners) {
        for (size_t axis = 0; axis < 3; ++axis) {
            const float value = vertex.position[axis];
            output.positions.push_back(value);
            output.bounds_min[axis] = std::min(output.bounds_min[axis], value);
            output.bounds_max[axis] = std::max(output.bounds_max[axis], value);
            output.normals.push_back(vertex.normal[axis]);
        }
        output.uvs.push_back(vertex.uv[0]);
        output.uvs.push_back(vertex.uv[1]);
    }
    output.indices = std::move(indices);
    result.primary_mesh = std::move(output);
    result.primary_mesh_extracted = true;
    return true;
}

} // namespace detail

// Parse one FBX into bounded scene metadata and optionally decode its largest
// triangle mesh as a primary mesh. This explicit selection avoids treating tiny
// helper geometry as the character; multi-mesh scene cooking remains a later
// step. Axis and unit conversion happen in ufbx at the import boundary: output
// positions use metres in Elisa's right-handed +Y-up frame.
inline FbxImportResult import_fbx(const std::filesystem::path& path,
    bool decode_first_mesh = true) {
    FbxImportResult result;
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
        detail::fail(result, detail::ufbx_error_text(error));
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
    if (decode_first_mesh && !detail::extract_primary_mesh(*scene, result)) return result;
    result.ok = true;
    return result;
}

} // namespace elisa::assets
