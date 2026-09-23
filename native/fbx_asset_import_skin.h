#pragma once

#include "fbx_asset_import_support.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace elisa::assets::detail {

inline bool append_skin_rig(const ufbx_scene& scene, const ufbx_skin_deformer& skin,
    FbxMeshData& output, FbxImportResult& result) {
    std::unordered_set<const ufbx_node*> included;
    std::unordered_map<const ufbx_node*, ufbx_matrix> bind_world;
    for (const ufbx_skin_cluster* cluster : skin.clusters) {
        if (cluster == nullptr || cluster->bone_node == nullptr) {
            fail(result, "primary FBX skin contains a cluster without a bone node");
            return false;
        }
        // A node's default pose can be an arbitrary animation frame. Skin
        // clusters carry the actual bind pose used by the mesh weights.
        bind_world.emplace(cluster->bone_node, cluster->bind_to_world);
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
    for (const ufbx_node* node : ordered) {
        bind_world.emplace(node, node->node_to_world);
    }
    output.skin_joints.reserve(ordered.size());
    for (const ufbx_node* node : ordered) {
        ufbx_matrix local_bind = bind_world.at(node);
        if (node->parent != nullptr) {
            const ufbx_matrix parent_inverse = ufbx_matrix_invert(&bind_world.at(node->parent));
            local_bind = ufbx_matrix_mul(&parent_inverse, &local_bind);
        }
        const ufbx_transform transform = ufbx_matrix_to_transform(&local_bind);
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
            fail(result, "primary FBX asset exceeds the runtime animation clip limit");
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

} // namespace elisa::assets::detail
