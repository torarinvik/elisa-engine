#pragma once

#include "fbx_asset_import_skin.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace elisa::assets::detail {

inline constexpr size_t MAX_FBX_MATERIAL_SLOTS = 16;

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

inline bool fbx_name_equals(ufbx_string name, const std::string& selected_name) {
    return name.data != nullptr && name.length == selected_name.size() &&
        std::equal(name.data, name.data + name.length, selected_name.begin());
}

inline bool extract_primary_mesh(ufbx_scene& scene, FbxImportResult& result,
    const std::string& selected_name) {
    ufbx_node* source_node = nullptr;
    for (size_t index = 0; index < scene.nodes.count; ++index) {
        ufbx_node* node = scene.nodes.data[index];
        if (node == nullptr || node->mesh == nullptr || node->mesh->num_triangles == 0) continue;
        if (!selected_name.empty()) {
            if (!fbx_name_equals(node->name, selected_name) &&
                !fbx_name_equals(node->mesh->name, selected_name)) continue;
            if (source_node != nullptr) {
                fail(result, "FBX mesh selector is ambiguous; use an exact unique node or mesh name");
                return false;
            }
            source_node = node;
        } else if (source_node == nullptr || node->mesh->num_triangles > source_node->mesh->num_triangles) {
            source_node = node;
        }
    }
    if (source_node == nullptr) {
        fail(result, selected_name.empty() ? "FBX scene has no triangle mesh node" :
            "FBX mesh selector did not match a triangle mesh name");
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
    if (corner_count > std::numeric_limits<uint32_t>::max()) {
        fail(result, "FBX mesh exceeds the bounded extracted-geometry budget");
        return false;
    }

    FbxMeshData output;
    output.node_name.assign(source_node->name.data ? source_node->name.data : "",
        source_node->name.length);
    output.mesh_name.assign(mesh.name.data ? mesh.name.data : "", mesh.name.length);
    const size_t material_count = source_node->materials.count != 0
        ? source_node->materials.count : mesh.materials.count;
    if (material_count > MAX_FBX_MATERIAL_SLOTS ||
        (mesh.face_material.count != 0 && mesh.face_material.count != mesh.faces.count)) {
        fail(result, "FBX mesh material slots exceed the bounded 16-slot limit or have invalid face assignments");
        return false;
    }
    output.material_slots = uint32_t(std::max<size_t>(1, material_count));
    std::vector<FbxVertex> corners;
    std::vector<FbxSkinInfluence> skin_corners;
    std::vector<uint32_t> triangle_material_slots;
    std::vector<uint32_t> indices(corner_count);
    corners.reserve(corner_count);
    std::vector<uint32_t> triangle_indices(mesh.max_face_triangles * 3);
    const ufbx_matrix normal_matrix = ufbx_matrix_for_normals(&source_node->geometry_to_world);

    const ufbx_skin_deformer* skin = nullptr;
    if (mesh.skin_deformers.count > 1) {
        fail(result, "multiple skin deformers on the primary FBX mesh are not supported yet");
        return false;
    }
    if (mesh.skin_deformers.count == 1) {
        skin = mesh.skin_deformers.data[0];
        if (skin == nullptr || skin->clusters.count == 0 || skin->clusters.count > 64 ||
            skin->vertices.count != mesh.num_vertices) {
            fail(result, "primary FBX skin exceeds the bounded bone or vertex limits");
            return false;
        }
        output.skin_bone_names.reserve(skin->clusters.count);
        for (const ufbx_skin_cluster* cluster : skin->clusters) {
            if (cluster == nullptr || cluster->bone_node == nullptr) {
                fail(result, "primary FBX skin contains a cluster without a bone node");
                return false;
            }
            if (cluster->bone_node->name.data == nullptr && cluster->bone_node->name.length != 0) {
                fail(result, "primary FBX skin bone name has an invalid data range");
                return false;
            }
            output.skin_bone_names.emplace_back(cluster->bone_node->name.data != nullptr
                ? cluster->bone_node->name.data : "", cluster->bone_node->name.length);
        }
        if (!append_skin_rig(scene, *skin, output, result)) return false;
    }
    const size_t bytes_per_corner = sizeof(FbxVertex) +
        (skin != nullptr ? sizeof(FbxSkinInfluence) : 0);
    if (corner_count > MAX_EXTRACTED_BYTES / bytes_per_corner) {
        fail(result, "FBX mesh exceeds the bounded extracted-geometry budget");
        return false;
    }
    if (skin != nullptr) skin_corners.reserve(corner_count);

    // Keep static geometry compact: unskinned meshes such as the Arc Gate do
    // not pay for four zeroed bone indices and weights per triangle corner.
    // For skinned data, ufbx indexes the base attributes and influences as
    // separate streams so vertices with different weights remain distinct.
    for (size_t face_index = 0; face_index < mesh.faces.count; ++face_index) {
        const ufbx_face face = mesh.faces.data[face_index];
        if (face.num_indices < 3) continue;
        uint32_t material_slot = mesh.face_material.count != 0 ? mesh.face_material.data[face_index] : 0;
        if (material_slot == UFBX_NO_INDEX) material_slot = 0;
        if (material_slot >= output.material_slots) {
            fail(result, "FBX face references a material outside its mesh slot table");
            return false;
        }
        const size_t triangle_count = ufbx_triangulate_face(
            triangle_indices.data(), triangle_indices.size(), &mesh, face);
        if (triangle_count == 0 || triangle_count > mesh.max_face_triangles) {
            fail(result, "ufbx could not triangulate an FBX mesh face");
            return false;
        }
        for (size_t triangle = 0; triangle < triangle_count; ++triangle)
            triangle_material_slots.push_back(material_slot);
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
            FbxSkinInfluence influence;
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
            if (skin != nullptr) {
                if (index >= mesh.vertex_indices.count) {
                    fail(result, "FBX skin vertex index is out of range");
                    return false;
                }
                const uint32_t source_vertex = mesh.vertex_indices.data[index];
                if (source_vertex >= skin->vertices.count) {
                    fail(result, "FBX skin source vertex is out of range");
                    return false;
                }
                const ufbx_skin_vertex& skin_vertex = skin->vertices.data[source_vertex];
                if (skin_vertex.weight_begin > skin->weights.count ||
                    skin_vertex.num_weights > skin->weights.count - skin_vertex.weight_begin) {
                    fail(result, "FBX skin weight range is out of bounds");
                    return false;
                }
                double total_weight = 0.0;
                for (size_t weight_index = 0; weight_index < skin_vertex.num_weights; ++weight_index) {
                    const ufbx_skin_weight& source_weight =
                        skin->weights.data[skin_vertex.weight_begin + weight_index];
                    if (source_weight.cluster_index >= skin->clusters.count ||
                        !std::isfinite(double(source_weight.weight)) || source_weight.weight < 0.0) {
                        fail(result, "FBX skin contains an invalid bone influence");
                        return false;
                    }
                    const float weight = float(source_weight.weight);
                    if (weight == 0.0f) continue;
                    size_t insert_at = 4;
                    for (size_t slot = 0; slot < 4; ++slot) {
                        if (weight > influence.bone_weights[slot]) {
                            insert_at = slot;
                            break;
                        }
                    }
                    if (insert_at < 4) {
                        for (size_t slot = 3; slot > insert_at; --slot) {
                            influence.bone_indices[slot] = influence.bone_indices[slot - 1];
                            influence.bone_weights[slot] = influence.bone_weights[slot - 1];
                        }
                        influence.bone_indices[insert_at] = source_weight.cluster_index;
                        influence.bone_weights[insert_at] = weight;
                    }
                }
                for (float weight : influence.bone_weights) total_weight += weight;
                if (total_weight > 1.0e-20 && std::isfinite(total_weight)) {
                    for (float& weight : influence.bone_weights) weight = float(double(weight) / total_weight);
                } else {
                    fail(result, "FBX skin vertex has no positive bone influence");
                    return false;
                }
                skin_corners.push_back(influence);
            }
            corners.push_back(vertex);
        }
    }
    if (corners.size() != corner_count || triangle_material_slots.size() != mesh.num_triangles) {
        fail(result, "FBX face triangulation disagrees with the reported triangle count");
        return false;
    }

    for (size_t index = 0; index < corner_count; ++index) indices[index] = uint32_t(index);
    ufbx_vertex_stream streams[2] = {
        {corners.data(), corners.size(), sizeof(FbxVertex)},
        {skin_corners.data(), skin_corners.size(), sizeof(FbxSkinInfluence)},
    };
    ufbx_allocator_opts index_allocator{};
    index_allocator.memory_limit = MAX_INDEX_MEMORY_BYTES;
    ufbx_error index_error{};
    const size_t unique_count = ufbx_generate_indices(streams, skin != nullptr ? 2 : 1,
        indices.data(), corner_count, &index_allocator, &index_error);
    if (unique_count == 0 || unique_count > corners.size() || index_error.type != UFBX_ERROR_NONE) {
        fail(result, index_error.type == UFBX_ERROR_NONE
            ? "FBX vertex indexing produced no vertices"
            : "FBX vertex indexing failed (ufbx error " + std::to_string(int(index_error.type)) + "): " + ufbx_error_text(index_error));
        return false;
    }
    corners.resize(unique_count);
    if (skin != nullptr) skin_corners.resize(unique_count);
    indices.resize(corner_count);
    std::vector<uint32_t> material_ordered_indices;
    material_ordered_indices.reserve(corner_count);
    for (uint32_t slot = 0; slot < output.material_slots; ++slot) {
        const uint32_t subset_start = uint32_t(material_ordered_indices.size());
        for (size_t triangle = 0; triangle < triangle_material_slots.size(); ++triangle) {
            if (triangle_material_slots[triangle] != slot) continue;
            material_ordered_indices.push_back(indices[triangle * 3]);
            material_ordered_indices.push_back(indices[triangle * 3 + 1]);
            material_ordered_indices.push_back(indices[triangle * 3 + 2]);
        }
        const uint32_t subset_count = uint32_t(material_ordered_indices.size()) - subset_start;
        if (subset_count != 0) output.subsets.push_back({subset_start, subset_count, slot});
    }
    indices.swap(material_ordered_indices);
    output.positions.reserve(unique_count * 3);
    output.normals.reserve(unique_count * 3);
    output.uvs.reserve(unique_count * 2);
    if (skin != nullptr) {
        output.skin_indices.reserve(unique_count * 4);
        output.skin_weights.reserve(unique_count * 4);
    }
    for (size_t index = 0; index < corners.size(); ++index) {
        const FbxVertex& vertex = corners[index];
        for (size_t axis = 0; axis < 3; ++axis) {
            const float value = vertex.position[axis];
            output.positions.push_back(value);
            output.bounds_min[axis] = std::min(output.bounds_min[axis], value);
            output.bounds_max[axis] = std::max(output.bounds_max[axis], value);
            output.normals.push_back(vertex.normal[axis]);
        }
        output.uvs.push_back(vertex.uv[0]);
        output.uvs.push_back(vertex.uv[1]);
        if (skin != nullptr) {
            const FbxSkinInfluence& skin_influence = skin_corners[index];
            for (size_t slot = 0; slot < 4; ++slot) {
                output.skin_indices.push_back(skin_influence.bone_indices[slot]);
                output.skin_weights.push_back(skin_influence.bone_weights[slot]);
            }
        }
    }
    output.indices = std::move(indices);
    result.primary_mesh = std::move(output);
    result.primary_mesh_extracted = true;
    return true;
}

} // namespace elisa::assets::detail
