#pragma once

#include "cooked_geometry_package.h"
#include "cooked_scene_bridge.h"
#include "coordinate_conventions.h"
#include "wiScene.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace elisa::rendering {

struct JointEntityRollback {
    wi::scene::Scene& scene;
    std::vector<wi::ecs::Entity>& entities;
    bool committed = false;

    ~JointEntityRollback() {
        if (!committed) {
            for (wi::ecs::Entity entity : entities) scene.Entity_Remove(entity);
        }
    }
};

// Every render-service transform arrives in Elisa's right-handed space and is
// reflected into Wicked's here, once (probe::coordinates::to_wicked).
inline void set_transform(wi::scene::TransformComponent& transform,
    float px, float py, float pz,
    float qx, float qy, float qz, float qw,
    float sx, float sy, float sz) {
    const double length = std::sqrt(double(qx) * qx + double(qy) * qy +
        double(qz) * qz + double(qw) * qw);
    const float inverse_length = float(1.0 / length);
    transform.translation_local = probe::coordinates::to_wicked(px, py, pz);
    transform.rotation_local = probe::coordinates::to_wicked_rotation(qx * inverse_length,
        qy * inverse_length, qz * inverse_length, qw * inverse_length);
    transform.scale_local = XMFLOAT3(sx, sy, sz);
    transform.SetDirty();
    transform.UpdateTransform();
}

// Cooked vertex streams are authored in Elisa space too.
inline XMFLOAT3 cooked_vector(const std::vector<float>& values, size_t index) {
    return probe::coordinates::to_wicked(values[index * 3], values[index * 3 + 1], values[index * 3 + 2]);
}

inline XMFLOAT4 cooked_tangent(const std::vector<float>& values, size_t index) {
    return probe::coordinates::to_wicked_tangent(values[index * 4], values[index * 4 + 1],
        values[index * 4 + 2], values[index * 4 + 3]);
}

// The reflection reverses every triangle (coordinates::winding_reversed), which
// turns Elisa's counter-clockwise front faces into the order Wicked rasterizes
// as front-facing, so indices keep their authored order. Instance scales are
// validated positive and never reflect a mesh a second time.
inline void assign_cooked_indices(wi::scene::MeshComponent& mesh, const std::vector<uint32_t>& indices) {
    mesh.indices.assign(indices.begin(), indices.end());
    if (probe::coordinates::winding_reversed(XMFLOAT3(1.0f, 1.0f, 1.0f))) return;
    for (size_t triangle = 0; triangle < mesh.indices.size(); triangle += 3) {
        std::swap(mesh.indices[triangle + 1], mesh.indices[triangle + 2]);
    }
}

inline bool configure_cooked_mesh(wi::scene::Scene& scene, wi::ecs::Entity entity,
    const elisa::assets::CookedGeometry& geometry,
    float px, float py, float pz, float qx, float qy, float qz, float qw,
    float sx, float sy, float sz, float red, float green, float blue, float alpha,
    std::vector<wi::ecs::Entity>* out_joint_entities = nullptr,
    size_t placement_index = std::numeric_limits<size_t>::max(),
    wi::ecs::Entity shared_armature = wi::ecs::INVALID_ENTITY) {
    const bool use_placement = placement_index != std::numeric_limits<size_t>::max();
    size_t vertex_start = 0;
    size_t vertex_count = geometry.positions.size() / 3;
    size_t index_start = 0;
    size_t index_count = geometry.indices.size();
    size_t subset_start = 0;
    size_t subset_count = geometry.subsets.size();
    if (use_placement) {
        if (placement_index >= geometry.mesh_placements.size()) return false;
        const auto& placement = geometry.mesh_placements[placement_index];
        vertex_start = placement.vertex_start;
        vertex_count = placement.vertex_count;
        index_start = placement.index_start;
        index_count = placement.index_count;
        subset_start = placement.subset_start;
        subset_count = placement.subset_count;
    }
    wi::scene::TransformComponent* transform = scene.transforms.GetComponent(entity);
    wi::scene::MaterialComponent* material = scene.materials.GetComponent(entity);
    wi::scene::ObjectComponent* object = scene.objects.GetComponent(entity);
    wi::scene::MeshComponent* mesh = scene.meshes.GetComponent(entity);
    if (transform == nullptr || material == nullptr || object == nullptr || mesh == nullptr ||
        geometry.positions.size() % 3 != 0 || geometry.normals.size() != geometry.positions.size() ||
        geometry.uvs.size() != geometry.positions.size() / 3 * 2 ||
        (!geometry.uv1s.empty() && geometry.uv1s.size() != geometry.positions.size() / 3 * 2) ||
        geometry.indices.empty() ||
        (!geometry.tangents.empty() && geometry.tangents.size() != geometry.positions.size() / 3 * 4) ||
        geometry.indices.size() % 3 != 0 ||
        geometry.indices.size() > std::numeric_limits<uint32_t>::max() || vertex_count == 0 ||
        index_count == 0 || index_count % 3 != 0 || vertex_start + vertex_count > geometry.positions.size() / 3 ||
        index_start + index_count > geometry.indices.size() || subset_count == 0 ||
        subset_start + subset_count > geometry.subsets.size()) return false;
    for (const auto& target : geometry.morph_targets) {
        if (target.positions.size() != geometry.positions.size() ||
            (!target.normals.empty() && target.normals.size() != geometry.positions.size())) return false;
    }
    const bool has_skin_data = !geometry.skin_joints.empty() || !geometry.skin_cluster_joints.empty() ||
        !geometry.skin_indices.empty() || !geometry.skin_weights.empty();
    const bool has_skin_rig = !geometry.skin_joints.empty() && !geometry.skin_cluster_joints.empty();
    if (has_skin_data != has_skin_rig || (shared_armature != wi::ecs::INVALID_ENTITY &&
        (!has_skin_rig || out_joint_entities != nullptr || scene.armatures.GetComponent(shared_armature) == nullptr))) {
        return false;
    }
    if (has_skin_rig && (geometry.skin_joints.size() > elisa::assets::MAX_GEOMETRY_RIG_NODES ||
        geometry.skin_cluster_joints.size() > elisa::assets::MAX_GEOMETRY_SKIN_BONES ||
        geometry.skin_indices.size() != geometry.positions.size() / 3 * 4 ||
        geometry.skin_weights.size() != geometry.skin_indices.size() ||
        (!geometry.skin_inverse_bind_matrices.empty() &&
            geometry.skin_inverse_bind_matrices.size() != geometry.skin_cluster_joints.size() * 16) ||
        (shared_armature == wi::ecs::INVALID_ENTITY && out_joint_entities == nullptr))) return false;

    mesh->vertex_positions.resize(vertex_count);
    mesh->vertex_normals.resize(vertex_count);
    // Entity_CreateCube prepares tangent data for its starter geometry. Do not
    // retain any of that mesh's per-vertex streams when replacing the geometry.
    mesh->vertex_tangents.clear();
    mesh->vertex_uvset_0.resize(vertex_count);
    mesh->vertex_uvset_1.resize(geometry.uv1s.empty() ? 0 : vertex_count);
    mesh->vertex_boneindices.clear();
    mesh->vertex_boneweights.clear();
    mesh->vertex_boneindices2.clear();
    mesh->vertex_boneweights2.clear();
    mesh->vertex_atlas.clear();
    mesh->vertex_colors.clear();
    mesh->vertex_windweights.clear();
    mesh->morph_targets.clear();
    for (size_t index = 0; index < mesh->vertex_positions.size(); ++index) {
        const size_t source = vertex_start + index;
        mesh->vertex_positions[index] = cooked_vector(geometry.positions, source);
        mesh->vertex_normals[index] = cooked_vector(geometry.normals, source);
        mesh->vertex_uvset_0[index] = XMFLOAT2(geometry.uvs[source * 2], geometry.uvs[source * 2 + 1]);
        if (!geometry.uv1s.empty()) {
            mesh->vertex_uvset_1[index] = XMFLOAT2(geometry.uv1s[source * 2], geometry.uv1s[source * 2 + 1]);
        }
    }
    mesh->morph_targets.resize(geometry.morph_targets.size());
    for (size_t target_index = 0; target_index < geometry.morph_targets.size(); ++target_index) {
        auto& target = mesh->morph_targets[target_index];
        const auto& cooked = geometry.morph_targets[target_index];
        target.vertex_positions.resize(mesh->vertex_positions.size());
        for (size_t vertex = 0; vertex < target.vertex_positions.size(); ++vertex)
            target.vertex_positions[vertex] = cooked_vector(cooked.positions, vertex_start + vertex);
        if (!cooked.normals.empty()) {
            target.vertex_normals.resize(mesh->vertex_positions.size());
            for (size_t vertex = 0; vertex < target.vertex_normals.size(); ++vertex)
                target.vertex_normals[vertex] = cooked_vector(cooked.normals, vertex_start + vertex);
        }
        const size_t placement_count = geometry.mesh_placements.empty() ? 1 : geometry.mesh_placements.size();
        const size_t placement = use_placement ? placement_index : 0;
        const size_t weight_index = placement * geometry.morph_targets.size() + target_index;
        if (placement >= placement_count || weight_index >= geometry.morph_default_weights.size()) return false;
        target.weight = geometry.morph_default_weights[weight_index];
    }
    set_transform(*transform, px, py, pz, qx, qy, qz, qw, sx, sy, sz);
    if (!geometry.tangents.empty()) {
        mesh->vertex_tangents.resize(vertex_count);
        for (size_t index = 0; index < mesh->vertex_tangents.size(); ++index) {
            mesh->vertex_tangents[index] = cooked_tangent(geometry.tangents, vertex_start + index);
        }
    }
    if (use_placement) {
        std::vector<uint32_t> local_indices;
        local_indices.reserve(index_count);
        for (size_t offset = 0; offset < index_count; ++offset) {
            const uint32_t source = geometry.indices[index_start + offset];
            if (source < vertex_start || size_t(source) >= vertex_start + vertex_count) return false;
            local_indices.push_back(uint32_t(size_t(source) - vertex_start));
        }
        assign_cooked_indices(*mesh, local_indices);
    } else {
        assign_cooked_indices(*mesh, geometry.indices);
    }
    std::vector<wi::ecs::Entity> joint_entities;
    JointEntityRollback joint_rollback{scene, joint_entities};
    if (has_skin_rig) {
        mesh->vertex_boneindices.resize(vertex_count);
        mesh->vertex_boneweights.resize(vertex_count);
        for (size_t vertex = 0; vertex < vertex_count; ++vertex) {
            const size_t offset = (vertex_start + vertex) * 4;
            for (size_t influence = 0; influence < 4; ++influence) {
                if (geometry.skin_weights[offset + influence] > 0.0f &&
                    geometry.skin_indices[offset + influence] >= geometry.skin_cluster_joints.size()) return false;
            }
            mesh->vertex_boneindices[vertex] = XMUINT4(geometry.skin_indices[offset],
                geometry.skin_indices[offset + 1], geometry.skin_indices[offset + 2], geometry.skin_indices[offset + 3]);
            mesh->vertex_boneweights[vertex] = XMFLOAT4(geometry.skin_weights[offset],
                geometry.skin_weights[offset + 1], geometry.skin_weights[offset + 2], geometry.skin_weights[offset + 3]);
        }
        if (shared_armature != wi::ecs::INVALID_ENTITY) {
            mesh->armatureID = shared_armature;
        } else {
            wi::scene::ArmatureComponent& armature = scene.armatures.Create(entity);
            armature.boneCollection.reserve(geometry.skin_cluster_joints.size());
            armature.inverseBindMatrices.reserve(geometry.skin_cluster_joints.size());
            joint_entities.reserve(geometry.skin_joints.size());
            for (size_t joint_index = 0; joint_index < geometry.skin_joints.size(); ++joint_index) {
                const auto& joint = geometry.skin_joints[joint_index];
                if (joint.parent_index < -1 || joint.parent_index >= int32_t(joint_index) ||
                    (joint.parent_index >= 0 && size_t(joint.parent_index) >= joint_entities.size())) return false;
                const std::string name = "elisa_skin_joint_" + std::to_string(uint32_t(entity)) + "_" +
                    std::to_string(joint_index);
                const wi::ecs::Entity joint_entity = scene.Entity_CreateTransform(name);
                if (joint_entity == wi::ecs::INVALID_ENTITY) return false;
                wi::scene::TransformComponent* joint_transform = scene.transforms.GetComponent(joint_entity);
                if (joint_transform == nullptr) return false;
                const auto& local = joint.rest_local;
                set_transform(*joint_transform, local[0], local[1], local[2], local[3], local[4],
                    local[5], local[6], local[7], local[8], local[9]);
                const wi::ecs::Entity parent = joint.parent_index < 0 ? entity : joint_entities[size_t(joint.parent_index)];
                scene.Component_Attach(joint_entity, parent, true);
                const wi::scene::TransformComponent* parent_transform = scene.transforms.GetComponent(parent);
                if (parent_transform == nullptr) return false;
                joint_transform->UpdateTransform_Parented(*parent_transform);
                joint_entities.push_back(joint_entity);
            }
            // Creating joint entities may reallocate Wicked's component stores;
            // reacquire the root pointers before deriving inverse bind matrices.
            transform = scene.transforms.GetComponent(entity);
            material = scene.materials.GetComponent(entity);
            object = scene.objects.GetComponent(entity);
            mesh = scene.meshes.GetComponent(entity);
            if (transform == nullptr || material == nullptr || object == nullptr || mesh == nullptr) return false;
            const XMMATRIX armature_inverse = XMMatrixInverse(nullptr, XMLoadFloat4x4(&transform->world));
            for (size_t cluster_index = 0; cluster_index < geometry.skin_cluster_joints.size(); ++cluster_index) {
                const uint32_t joint_index = geometry.skin_cluster_joints[cluster_index];
                if (joint_index >= joint_entities.size()) return false;
                const wi::ecs::Entity bone_entity = joint_entities[joint_index];
                XMFLOAT4X4 inverse_bind;
                if (!geometry.skin_inverse_bind_matrices.empty()) {
                    inverse_bind = probe::coordinates::gltf_inverse_bind_to_wicked(
                        geometry.skin_inverse_bind_matrices.data() + cluster_index * 16);
                } else {
                    const wi::scene::TransformComponent* bone_transform = scene.transforms.GetComponent(bone_entity);
                    if (bone_transform == nullptr) return false;
                    const XMMATRIX bone_local = XMMatrixMultiply(
                        XMLoadFloat4x4(&bone_transform->world), armature_inverse);
                    XMStoreFloat4x4(&inverse_bind, XMMatrixInverse(nullptr, bone_local));
                }
                const float* matrix_values = &inverse_bind.m[0][0];
                for (size_t component = 0; component < 16; ++component) {
                    if (!std::isfinite(matrix_values[component])) return false;
                }
                armature.boneCollection.push_back(bone_entity);
                armature.inverseBindMatrices.push_back(inverse_bind);
            }
            mesh->armatureID = entity;
        }
    }
    if (geometry.subsets.empty()) return false;
    mesh->subsets.clear();
    size_t subset_coverage = 0;
    const size_t subset_index_end = use_placement ? mesh->indices.size() + index_start : mesh->indices.size();
    for (size_t subset_index = subset_start; subset_index < subset_start + subset_count; ++subset_index) {
        const auto& cooked = geometry.subsets[subset_index];
        const size_t cooked_end = size_t(cooked.index_start) + cooked.index_count;
        const size_t begin = use_placement ? std::max<size_t>(cooked.index_start, index_start) : cooked.index_start;
        const size_t end = use_placement ? std::min(cooked_end, index_start + index_count) : cooked_end;
        if (begin >= end || end > subset_index_end ||
            (begin - index_start) % 3 != 0 || (end - begin) % 3 != 0) return false;
        wi::scene::MeshComponent::MeshSubset& subset = mesh->subsets.emplace_back();
        subset.indexOffset = uint32_t(begin - index_start);
        subset.indexCount = uint32_t(end - begin);
        subset.materialID = entity;
        subset_coverage += end - begin;
    }
    if (mesh->subsets.empty() || subset_coverage != index_count) return false;
    mesh->CreateRenderData();
    if (out_joint_entities != nullptr) *out_joint_entities = std::move(joint_entities);
    joint_rollback.committed = true;
    material->shaderType = wi::scene::MaterialComponent::SHADERTYPE_UNLIT;
    material->SetBaseColor(XMFLOAT4(red, green, blue, alpha));
    material->SetCastShadow(false);
    material->userBlendMode = alpha < 0.999f ? wi::enums::BLENDMODE_ALPHA : wi::enums::BLENDMODE_OPAQUE;
    object->SetCastShadow(false);
    return true;
}

} // namespace elisa::rendering
