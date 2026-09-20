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
#include <iterator>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace elisa::assets {

struct FbxVertex {
    float position[3]{};
    float normal[3]{};
    float uv[2]{};
};
static_assert(sizeof(FbxVertex) == sizeof(float) * 8);

struct FbxSkinInfluence {
    uint32_t bone_indices[4]{};
    float bone_weights[4]{};
};
static_assert(sizeof(FbxSkinInfluence) == sizeof(float) * 4 + sizeof(uint32_t) * 4);

struct FbxSkinJoint {
    std::string name;
    int32_t parent_index = -1;
    // Translation, quaternion xyzw, and scale, in the importer-normalized
    // metre-space hierarchy. Ancestor helpers are included when needed.
    float rest_local[10]{};
    int32_t cluster_index = -1;
};

struct FbxAnimationClip {
    std::string name;
    float duration_seconds = 0.0f;
    uint32_t sample_rate = 0;
    uint32_t frame_count = 0;
    // Frame-major; each joint has translation xyz, quaternion xyzw, scale xyz.
    std::vector<float> local_transforms;
};

struct FbxMeshData {
    std::string node_name;
    std::string mesh_name;
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> uvs;
    std::vector<float> tangents;
    std::vector<uint32_t> indices;
    std::vector<std::string> skin_bone_names;
    std::vector<uint32_t> skin_indices;
    std::vector<float> skin_weights;
    std::vector<FbxSkinJoint> skin_joints;
    std::vector<uint32_t> skin_cluster_joints;
    std::vector<FbxAnimationClip> animation_clips;
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
constexpr size_t MAX_SKIN_JOINTS = 64;
constexpr size_t MAX_ANIMATION_CLIPS = 8;
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

inline bool append_skin_rig(const ufbx_scene& scene, const ufbx_skin_deformer& skin,
    FbxMeshData& output, FbxImportResult& result) {
    std::unordered_set<const ufbx_node*> included;
    for (const ufbx_skin_cluster* cluster : skin.clusters) {
        if (cluster == nullptr || cluster->bone_node == nullptr) {
            fail(result, "primary FBX skin contains a cluster without a bone node");
            return false;
        }
        for (const ufbx_node* node = cluster->bone_node; node != nullptr; node = node->parent) {
            included.insert(node);
            if (included.size() > MAX_SKIN_JOINTS) {
                fail(result, "primary FBX rig hierarchy exceeds the 64-joint runtime limit");
                return false;
            }
        }
    }

    std::vector<const ufbx_node*> ordered(included.begin(), included.end());
    const auto depth = [](const ufbx_node* node) {
        size_t value = 0;
        for (const ufbx_node* parent = node->parent; parent != nullptr; parent = parent->parent) ++value;
        return value;
    };
    std::sort(ordered.begin(), ordered.end(), [&](const ufbx_node* left, const ufbx_node* right) {
        const size_t left_depth = depth(left);
        const size_t right_depth = depth(right);
        if (left_depth != right_depth) return left_depth < right_depth;
        if (left->element_id != right->element_id) return left->element_id < right->element_id;
        return std::string(left->name.data ? left->name.data : "", left->name.length) <
            std::string(right->name.data ? right->name.data : "", right->name.length);
    });

    std::unordered_map<const ufbx_node*, uint32_t> joint_indices;
    output.skin_joints.reserve(ordered.size());
    for (const ufbx_node* node : ordered) {
        const ufbx_transform& transform = node->local_transform;
        if (!finite(transform.translation) || !finite(transform.rotation) || !finite(transform.scale) ||
            (node->name.data == nullptr && node->name.length != 0)) {
            fail(result, "primary FBX rig contains an invalid joint transform or name");
            return false;
        }
        const double rotation_length = std::sqrt(double(transform.rotation.x) * transform.rotation.x +
            double(transform.rotation.y) * transform.rotation.y + double(transform.rotation.z) * transform.rotation.z +
            double(transform.rotation.w) * transform.rotation.w);
        if (!(rotation_length > 1.0e-12) || !std::isfinite(rotation_length) ||
            transform.scale.x == 0.0 || transform.scale.y == 0.0 || transform.scale.z == 0.0) {
            fail(result, "primary FBX rig contains a singular joint transform");
            return false;
        }
        FbxSkinJoint joint;
        joint.name.assign(node->name.data ? node->name.data : "", node->name.length);
        if (node->parent != nullptr) {
            const auto parent = joint_indices.find(node->parent);
            if (parent == joint_indices.end()) {
                fail(result, "primary FBX rig hierarchy is not parent ordered");
                return false;
            }
            joint.parent_index = int32_t(parent->second);
        }
        joint.rest_local[0] = float(transform.translation.x);
        joint.rest_local[1] = float(transform.translation.y);
        joint.rest_local[2] = float(transform.translation.z);
        joint.rest_local[3] = float(transform.rotation.x / rotation_length);
        joint.rest_local[4] = float(transform.rotation.y / rotation_length);
        joint.rest_local[5] = float(transform.rotation.z / rotation_length);
        joint.rest_local[6] = float(transform.rotation.w / rotation_length);
        joint.rest_local[7] = float(transform.scale.x);
        joint.rest_local[8] = float(transform.scale.y);
        joint.rest_local[9] = float(transform.scale.z);
        if (!std::all_of(std::begin(joint.rest_local), std::end(joint.rest_local),
                [](float value) { return std::isfinite(value); })) {
            fail(result, "primary FBX rig transform exceeds normalized float range");
            return false;
        }
        const uint32_t index = uint32_t(output.skin_joints.size());
        output.skin_joints.push_back(std::move(joint));
        joint_indices.emplace(node, index);
    }

    output.skin_cluster_joints.reserve(skin.clusters.count);
    for (size_t cluster_index = 0; cluster_index < skin.clusters.count; ++cluster_index) {
        const ufbx_skin_cluster* cluster = skin.clusters.data[cluster_index];
        const auto joint = joint_indices.find(cluster->bone_node);
        if (joint == joint_indices.end()) {
            fail(result, "primary FBX cluster does not resolve into its rig hierarchy");
            return false;
        }
        FbxSkinJoint& skin_joint = output.skin_joints[joint->second];
        if (skin_joint.cluster_index >= 0) {
            fail(result, "primary FBX skin maps multiple clusters to one joint");
            return false;
        }
        skin_joint.cluster_index = int32_t(cluster_index);
        output.skin_cluster_joints.push_back(joint->second);
    }

    size_t total_sample_floats = 0;
    for (const ufbx_anim_stack* stack : scene.anim_stacks) {
        if (stack == nullptr || !std::isfinite(stack->time_begin) || !std::isfinite(stack->time_end)) {
            fail(result, "primary FBX rig contains an invalid animation stack");
            return false;
        }
        const double duration = stack->time_end - stack->time_begin;
        if (duration <= 1.0e-6) continue;
        if (stack->anim == nullptr || duration > 120.0) {
            fail(result, "primary FBX animation is missing or exceeds the 120-second limit");
            return false;
        }
        const double raw_frame_count = std::ceil(duration * double(ANIMATION_SAMPLE_RATE)) + 1.0;
        if (!std::isfinite(raw_frame_count) || raw_frame_count < 2.0 || raw_frame_count > 3601.0 ||
            output.skin_joints.size() > (MAX_ANIMATION_SAMPLE_FLOATS - total_sample_floats) / 10 / size_t(raw_frame_count)) {
            fail(result, "primary FBX animation exceeds the bounded cooked-sample budget");
            return false;
        }
        const size_t frame_count = size_t(raw_frame_count);
        const size_t sample_floats = output.skin_joints.size() * frame_count * 10;
        if (total_sample_floats > MAX_ANIMATION_SAMPLE_FLOATS - sample_floats) {
            fail(result, "primary FBX animations exceed the bounded cooked-sample budget");
            return false;
        }
        if (output.animation_clips.size() >= MAX_ANIMATION_CLIPS) {
            fail(result, "primary FBX asset exceeds the eight-clip runtime limit");
            return false;
        }

        FbxAnimationClip clip;
        if (stack->name.data == nullptr && stack->name.length != 0) {
            fail(result, "primary FBX animation has an invalid name range");
            return false;
        }
        clip.name.assign(stack->name.data ? stack->name.data : "", stack->name.length);
        clip.duration_seconds = float(duration);
        clip.sample_rate = ANIMATION_SAMPLE_RATE;
        clip.frame_count = uint32_t(frame_count);
        clip.local_transforms.reserve(sample_floats);
        for (size_t frame = 0; frame < frame_count; ++frame) {
            const double offset = std::min(double(frame) / double(ANIMATION_SAMPLE_RATE), duration);
            const double time = stack->time_begin + offset;
            for (const ufbx_node* node : ordered) {
                const ufbx_transform transform = ufbx_evaluate_transform(stack->anim, node, time);
                if (!finite(transform.translation) || !finite(transform.rotation) || !finite(transform.scale)) {
                    fail(result, "primary FBX animation evaluates to a non-finite transform");
                    return false;
                }
                const double rotation_length = std::sqrt(double(transform.rotation.x) * transform.rotation.x +
                    double(transform.rotation.y) * transform.rotation.y + double(transform.rotation.z) * transform.rotation.z +
                    double(transform.rotation.w) * transform.rotation.w);
                if (!(rotation_length > 1.0e-12) || !std::isfinite(rotation_length) ||
                    transform.scale.x == 0.0 || transform.scale.y == 0.0 || transform.scale.z == 0.0) {
                    fail(result, "primary FBX animation evaluates to a singular transform");
                    return false;
                }
                const float values[10] = {
                    float(transform.translation.x), float(transform.translation.y), float(transform.translation.z),
                    float(transform.rotation.x / rotation_length), float(transform.rotation.y / rotation_length),
                    float(transform.rotation.z / rotation_length), float(transform.rotation.w / rotation_length),
                    float(transform.scale.x), float(transform.scale.y), float(transform.scale.z),
                };
                if (!std::all_of(std::begin(values), std::end(values),
                        [](float value) { return std::isfinite(value); })) {
                    fail(result, "primary FBX animation transform exceeds normalized float range");
                    return false;
                }
                clip.local_transforms.insert(clip.local_transforms.end(), std::begin(values), std::end(values));
            }
        }
        total_sample_floats += sample_floats;
        output.animation_clips.push_back(std::move(clip));
    }
    return true;
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
    if (corner_count > std::numeric_limits<uint32_t>::max()) {
        fail(result, "FBX mesh exceeds the bounded extracted-geometry budget");
        return false;
    }

    FbxMeshData output;
    output.node_name.assign(source_node->name.data ? source_node->name.data : "",
        source_node->name.length);
    output.mesh_name.assign(mesh.name.data ? mesh.name.data : "", mesh.name.length);
    std::vector<FbxVertex> corners;
    std::vector<FbxSkinInfluence> skin_corners;
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
    if (corners.size() != corner_count) {
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
    if (decode_first_mesh && !detail::extract_primary_mesh(*scene, result)) return result;
    result.ok = true;
    return result;
}

} // namespace elisa::assets
