#pragma once

#include "fbx_asset_import_skin.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
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

inline bool fbx_unit_scalar(const ufbx_material_map& map, float fallback, float& value) {
    const double source = map.has_value ? double(map.value_real) : double(fallback);
    if (!std::isfinite(source) || source < 0.0 || source > 1.0) return false;
    value = float(source);
    return std::isfinite(value);
}

inline bool fbx_unit_color(const ufbx_material_map& map, const ufbx_material_map& fallback_map,
    const std::array<float, 4>& fallback, std::array<float, 4>& value) {
    const ufbx_material_map& source = map.has_value ? map : fallback_map;
    value = fallback;
    if (!source.has_value) return true;
    if (source.value_components < 3 || source.value_components > 4) return false;
    const double channels[4] = {double(source.value_vec4.x), double(source.value_vec4.y),
        double(source.value_vec4.z), source.value_components == 4 ? double(source.value_vec4.w) : 1.0};
    for (size_t channel = 0; channel < 4; ++channel) {
        if (!std::isfinite(channels[channel]) || channels[channel] < 0.0 || channels[channel] > 1.0) return false;
        value[channel] = float(channels[channel]);
    }
    return true;
}

inline bool fbx_map_has_texture(const ufbx_material_map& map) {
    return map.texture_enabled && map.texture != nullptr;
}

inline bool fbx_texture_source_path(const ufbx_material_map& map, std::string& path,
    FbxImportResult& result) {
    path.clear();
    if (!fbx_map_has_texture(map)) return true;
    const ufbx_texture& texture = *map.texture;
    if (texture.type != UFBX_TEXTURE_FILE || texture.content.size != 0 || texture.video != nullptr ||
        texture.has_uv_transform || texture.wrap_u != UFBX_WRAP_REPEAT ||
        texture.wrap_v != UFBX_WRAP_REPEAT || texture.uv_set.length != 0) {
        fail(result, "FBX textures must be direct external files with default UVs and repeat wrapping");
        return false;
    }
    const ufbx_string filename = texture.relative_filename.length != 0
        ? texture.relative_filename : texture.filename;
    if (filename.data == nullptr || filename.length == 0 || filename.length > 4096) {
        fail(result, "FBX texture has no bounded relative file path");
        return false;
    }
    path.assign(filename.data, filename.length);
    if (path.front() == '/' || path.find_first_of("\\:\r\n") != std::string::npos ||
        path.find('\0') != std::string::npos) {
        fail(result, "FBX texture paths must be safe relative paths");
        return false;
    }
    size_t start = 0;
    while (start < path.size()) {
        const size_t end = path.find('/', start);
        const std::string component = path.substr(start,
            end == std::string::npos ? std::string::npos : end - start);
        if (component.empty() || component == "." || component == "..") {
            fail(result, "FBX texture paths cannot contain empty, current, or parent directories");
            return false;
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}

inline bool fbx_extract_texture_role(const ufbx_material_map& preferred,
    const ufbx_material_map& fallback, std::string& path, FbxImportResult& result) {
    const ufbx_material_map* selected = nullptr;
    if (fbx_map_has_texture(preferred)) selected = &preferred;
    if (fbx_map_has_texture(fallback)) {
        if (selected != nullptr && selected->texture != fallback.texture) {
            fail(result, "FBX material has conflicting textures for one supported material role");
            return false;
        }
        selected = &fallback;
    }
    if (selected == nullptr) {
        path.clear();
        return true;
    }
    return fbx_texture_source_path(*selected, path, result);
}

inline bool extract_fbx_material(const ufbx_material& source, FbxMaterialData& output,
    FbxImportResult& result, bool ignore_textures = false) {
    const ufbx_material_map* supported[] = {
        &source.pbr.base_color, &source.fbx.diffuse_color,
        &source.pbr.normal_map, &source.fbx.normal_map,
        &source.pbr.emission_color, &source.fbx.emission_color,
        &source.pbr.ambient_occlusion, &source.pbr.roughness, &source.pbr.metalness,
    };
    if (!ignore_textures) {
        for (size_t index = 0; index < UFBX_MATERIAL_PBR_MAP_COUNT; ++index) {
            const ufbx_material_map& map = source.pbr.maps[index];
            if (!fbx_map_has_texture(map)) continue;
            bool allowed = false;
            for (const ufbx_material_map* candidate : supported) allowed = allowed || &map == candidate;
            if (!allowed) {
                fail(result, "FBX material uses a texture role that the runtime cannot represent yet");
                return false;
            }
        }
        for (size_t index = 0; index < UFBX_MATERIAL_FBX_MAP_COUNT; ++index) {
            const ufbx_material_map& map = source.fbx.maps[index];
            if (!fbx_map_has_texture(map)) continue;
            bool allowed = false;
            for (const ufbx_material_map* candidate : supported) allowed = allowed || &map == candidate;
            if (!allowed) {
                fail(result, "FBX material uses a texture role that the runtime cannot represent yet");
                return false;
            }
        }
    }
    if (source.name.length > 256 || (source.name.length != 0 && source.name.data == nullptr)) {
        fail(result, "FBX material name exceeds the 256-byte limit");
        return false;
    }
    output.name.assign(source.name.data ? source.name.data : "", source.name.length);
    if (output.name.find('\0') != std::string::npos) {
        fail(result, "FBX material name contains a null byte");
        return false;
    }

    constexpr std::array<float, 4> WHITE{1.0f, 1.0f, 1.0f, 1.0f};
    const ufbx_material_map& base_factor = source.pbr.base_factor.has_value
        ? source.pbr.base_factor : source.fbx.diffuse_factor;
    float base_multiplier = 1.0f;
    if (!fbx_unit_scalar(base_factor, 1.0f, base_multiplier) ||
        !fbx_unit_color(source.pbr.base_color, source.fbx.diffuse_color, WHITE, output.base_color)) {
        fail(result, "FBX material base-color factors must be finite and in [0, 1]");
        return false;
    }
    for (size_t channel = 0; channel < 3; ++channel) output.base_color[channel] *= base_multiplier;

    if (!fbx_unit_scalar(source.pbr.metalness, 0.0f, output.metallic) ||
        !fbx_unit_scalar(source.pbr.roughness, 1.0f, output.roughness)) {
        fail(result, "FBX metallic and roughness factors must be finite and in [0, 1]");
        return false;
    }
    constexpr std::array<float, 4> BLACK{0.0f, 0.0f, 0.0f, 1.0f};
    const ufbx_material_map& emission_factor = source.pbr.emission_factor.has_value
        ? source.pbr.emission_factor : source.fbx.emission_factor;
    const ufbx_material_map& emission_color = source.pbr.emission_color.has_value
        ? source.pbr.emission_color : source.fbx.emission_color;
    float emission_multiplier = 1.0f;
    std::array<float, 4> emission{};
    if (!fbx_unit_scalar(emission_factor, 1.0f, emission_multiplier) ||
        !fbx_unit_color(emission_color, source.fbx.emission_color, BLACK, emission)) {
        fail(result, "FBX emissive factors must be finite and in [0, 1]");
        return false;
    }
    for (size_t channel = 0; channel < 3; ++channel) {
        output.emissive[channel] = emission[channel] * emission_multiplier;
    }

    float transparency = 0.0f;
    if (!fbx_unit_scalar(source.fbx.transparency_factor, 0.0f, transparency)) {
        fail(result, "FBX transparency factor must be finite and in [0, 1]");
        return false;
    }
    output.base_color[3] *= 1.0f - transparency;
    output.alpha_mode = output.base_color[3] < 1.0f ? 2u : 0u;
    output.double_sided = source.features.double_sided.enabled;
    if (ignore_textures) return true;
    if (!fbx_extract_texture_role(source.pbr.base_color, source.fbx.diffuse_color,
            output.texture_sources[0], result) ||
        !fbx_extract_texture_role(source.pbr.normal_map, source.fbx.normal_map,
            output.texture_sources[1], result) ||
        !fbx_extract_texture_role(source.pbr.emission_color, source.fbx.emission_color,
            output.texture_sources[3], result) ||
        !fbx_texture_source_path(source.pbr.ambient_occlusion,
            output.texture_sources[4], result) ||
        !fbx_texture_source_path(source.pbr.roughness,
            output.surface_texture_sources[0], result) ||
        !fbx_texture_source_path(source.pbr.metalness,
            output.surface_texture_sources[1], result)) return false;
    output.occlusion = !output.texture_sources[4].empty();
    return true;
}

inline bool extract_mesh_node(ufbx_scene& scene, ufbx_node* source_node,
    FbxMeshData& output, FbxImportResult& result, bool ignore_textures) {
    if (source_node == nullptr || source_node->mesh == nullptr) {
        fail(result, "FBX scene mesh node is missing its geometry");
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
    if (material_count != 0) {
        const bool node_materials = source_node->materials.count != 0;
        const ufbx_material_list& materials = node_materials ? source_node->materials : mesh.materials;
        if (materials.count != material_count) {
            fail(result, "FBX mesh material slots do not match their material records");
            return false;
        }
        output.materials.reserve(material_count);
        for (const ufbx_material* material : materials) {
            if (material == nullptr) {
                fail(result, "FBX mesh contains a missing material record");
                return false;
            }
            FbxMaterialData extracted;
            if (!extract_fbx_material(*material, extracted, result, ignore_textures)) return false;
            output.materials.push_back(std::move(extracted));
        }
        const bool has_textures = std::any_of(output.materials.begin(), output.materials.end(),
            [](const FbxMaterialData& material) {
                return std::any_of(material.texture_sources.begin(), material.texture_sources.end(),
                    [](const std::string& path) { return !path.empty(); }) ||
                    std::any_of(material.surface_texture_sources.begin(), material.surface_texture_sources.end(),
                    [](const std::string& path) { return !path.empty(); });
            });
        if (has_textures && !mesh.vertex_uv.exists) {
            fail(result, "FBX material textures require UV coordinates on the primary mesh");
            return false;
        }
    }
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
    return true;
}

inline bool merge_static_mesh(FbxMeshData& target, FbxMeshData&& source,
    FbxImportResult& result) {
    const size_t vertex_base = target.positions.size() / 3;
    const size_t index_base = target.indices.size();
    const uint32_t material_base = target.material_slots;
    if (vertex_base > std::numeric_limits<uint32_t>::max() ||
        index_base > std::numeric_limits<uint32_t>::max() ||
        source.material_slots == 0 ||
        material_base > MAX_FBX_MATERIAL_SLOTS - source.material_slots) {
        fail(result, "combined FBX mesh exceeds the vertex, index, or 16 material-slot limit");
        return false;
    }
    if (!source.skin_indices.empty() || !source.animation_clips.empty() || !source.skin_joints.empty()) {
        fail(result, "--all-meshes currently supports static FBX scenes without skinning or animation");
        return false;
    }

    if (!target.materials.empty() || !source.materials.empty()) {
        target.materials.resize(target.material_slots);
        source.materials.resize(source.material_slots);
        target.materials.insert(target.materials.end(),
            std::make_move_iterator(source.materials.begin()),
            std::make_move_iterator(source.materials.end()));
    }
    target.positions.insert(target.positions.end(), source.positions.begin(), source.positions.end());
    target.normals.insert(target.normals.end(), source.normals.begin(), source.normals.end());
    target.uvs.insert(target.uvs.end(), source.uvs.begin(), source.uvs.end());
    target.indices.reserve(target.indices.size() + source.indices.size());
    for (uint32_t index : source.indices) {
        if (index >= source.positions.size() / 3 || vertex_base + index > std::numeric_limits<uint32_t>::max()) {
            fail(result, "combined FBX mesh contains an invalid vertex index");
            return false;
        }
        target.indices.push_back(uint32_t(vertex_base + index));
    }
    target.subsets.reserve(target.subsets.size() + source.subsets.size());
    for (const FbxMeshSubset& subset : source.subsets) {
        if (subset.index_start > source.indices.size() ||
            subset.index_count > source.indices.size() - subset.index_start ||
            index_base + subset.index_start > std::numeric_limits<uint32_t>::max()) {
            fail(result, "combined FBX mesh has an invalid subset range");
            return false;
        }
        target.subsets.push_back({uint32_t(index_base + subset.index_start), subset.index_count,
            material_base + subset.material_slot});
    }
    for (size_t axis = 0; axis < 3; ++axis) {
        target.bounds_min[axis] = std::min(target.bounds_min[axis], source.bounds_min[axis]);
        target.bounds_max[axis] = std::max(target.bounds_max[axis], source.bounds_max[axis]);
    }
    target.material_slots += source.material_slots;
    ++target.source_mesh_count;
    return true;
}

inline bool extract_scene_meshes(ufbx_scene& scene, FbxImportResult& result,
    const std::string& selected_name, bool all_meshes = false, bool ignore_textures = false) {
    if (all_meshes && !selected_name.empty()) {
        fail(result, "--all-meshes cannot be combined with an exact mesh selector");
        return false;
    }
    struct SourceMeshNode {
        size_t scene_index;
        ufbx_node* node;
    };
    std::vector<SourceMeshNode> source_nodes;
    ufbx_node* largest_node = nullptr;
    size_t largest_node_index = 0;
    for (size_t index = 0; index < scene.nodes.count; ++index) {
        ufbx_node* node = scene.nodes.data[index];
        if (node == nullptr || node->mesh == nullptr || node->mesh->num_triangles == 0) continue;
        if (!selected_name.empty() && !fbx_name_equals(node->name, selected_name) &&
            !fbx_name_equals(node->mesh->name, selected_name)) continue;
        if (!all_meshes && selected_name.empty()) {
            if (largest_node == nullptr || node->mesh->num_triangles > largest_node->mesh->num_triangles) {
                largest_node = node;
                largest_node_index = index;
            }
        } else {
            if (!all_meshes && !source_nodes.empty()) {
                fail(result, "FBX mesh selector is ambiguous; use an exact unique node or mesh name");
                return false;
            }
            if (all_meshes && (index >= 256 || source_nodes.size() >= 256)) {
                fail(result, "--all-meshes supports at most 256 source mesh placements and node indices below 256");
                return false;
            }
            source_nodes.push_back({index, node});
        }
    }
    if (selected_name.empty() && !all_meshes && largest_node != nullptr)
        source_nodes.push_back({largest_node_index, largest_node});
    if (source_nodes.empty()) {
        fail(result, selected_name.empty() ? "FBX scene has no triangle mesh node" :
            "FBX mesh selector did not match a triangle mesh name");
        return false;
    }
    if (all_meshes) {
        if (source_nodes.size() > MAX_FBX_SOURCE_MESH_COUNT) {
            fail(result, "--all-meshes exceeds the 1024 source-mesh limit");
            return false;
        }
        if (scene.anim_stacks.count != 0) {
            fail(result, "--all-meshes currently requires an FBX scene without animation stacks");
            return false;
        }
        size_t triangle_total = 0;
        for (const SourceMeshNode& source : source_nodes) {
            ufbx_node* node = source.node;
            const ufbx_mesh& mesh = *node->mesh;
            if (mesh.skin_deformers.count != 0 ||
                mesh.num_triangles > MAX_FBX_COMBINED_TRIANGLES - triangle_total) {
                fail(result, mesh.skin_deformers.count != 0 ?
                    "--all-meshes currently supports static FBX scenes without skinning or animation" :
                    "--all-meshes exceeds the one-million-triangle combined geometry limit");
                return false;
            }
            triangle_total += mesh.num_triangles;
        }
    }
    FbxMeshData combined;
    combined.material_slots = 1;
    if (all_meshes) {
        combined.material_slots = 0;
        combined.source_mesh_count = 0;
        for (const SourceMeshNode& source : source_nodes) {
            ufbx_node* node = source.node;
            FbxMeshData extracted;
            if (!extract_mesh_node(scene, node, extracted, result, ignore_textures)) return false;
            const size_t vertex_start = combined.positions.size() / 3;
            const size_t index_start = combined.indices.size();
            const size_t subset_start = combined.subsets.size();
            const size_t vertex_count = extracted.positions.size() / 3;
            const size_t index_count = extracted.indices.size();
            const size_t subset_count = extracted.subsets.size();
            if (vertex_start > std::numeric_limits<uint32_t>::max() ||
                index_start > std::numeric_limits<uint32_t>::max() ||
                subset_start > std::numeric_limits<uint32_t>::max() ||
                vertex_count > std::numeric_limits<uint32_t>::max() ||
                index_count > std::numeric_limits<uint32_t>::max() ||
                subset_count > std::numeric_limits<uint32_t>::max()) {
                fail(result, "combined FBX mesh placement exceeds the 32-bit range limit");
                return false;
            }
            FbxMeshPlacementData placement;
            placement.mesh = uint32_t(combined.mesh_placements.size());
            placement.node = uint32_t(source.scene_index);
            placement.vertex_start = uint32_t(vertex_start);
            placement.vertex_count = uint32_t(vertex_count);
            placement.index_start = uint32_t(index_start);
            placement.index_count = uint32_t(index_count);
            placement.subset_start = uint32_t(subset_start);
            placement.subset_count = uint32_t(subset_count);
            const ufbx_matrix& transform = node->geometry_to_world;
            placement.transform = {
                float(transform.m00), float(transform.m01), float(transform.m02), float(transform.m03),
                float(transform.m10), float(transform.m11), float(transform.m12), float(transform.m13),
                float(transform.m20), float(transform.m21), float(transform.m22), float(transform.m23),
            };
            if (!std::all_of(placement.transform.begin(), placement.transform.end(),
                [](float value) { return std::isfinite(value); })) {
                fail(result, "FBX placement transform exceeds the finite float range");
                return false;
            }
            if (!merge_static_mesh(combined, std::move(extracted), result)) return false;
            combined.mesh_placements.push_back(placement);
        }
    } else if (!extract_mesh_node(scene, source_nodes.front().node, combined, result, ignore_textures)) {
        return false;
    }
    result.primary_mesh = std::move(combined);
    result.primary_mesh_extracted = true;
    return true;
}

} // namespace elisa::assets::detail
