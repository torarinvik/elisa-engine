#pragma once

#include "cooked_geometry_package.h"
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
    std::vector<wi::ecs::Entity>* out_joint_entities = nullptr) {
    wi::scene::TransformComponent* transform = scene.transforms.GetComponent(entity);
    wi::scene::MaterialComponent* material = scene.materials.GetComponent(entity);
    wi::scene::ObjectComponent* object = scene.objects.GetComponent(entity);
    wi::scene::MeshComponent* mesh = scene.meshes.GetComponent(entity);
    if (transform == nullptr || material == nullptr || object == nullptr || mesh == nullptr ||
        geometry.positions.size() % 3 != 0 || geometry.normals.size() != geometry.positions.size() ||
        geometry.uvs.size() != geometry.positions.size() / 3 * 2 || geometry.indices.empty() ||
        (!geometry.tangents.empty() && geometry.tangents.size() != geometry.positions.size() / 3 * 4) ||
        geometry.indices.size() % 3 != 0 ||
        geometry.indices.size() > std::numeric_limits<uint32_t>::max()) return false;
    const bool has_skin_rig = !geometry.skin_joints.empty() && !geometry.skin_cluster_joints.empty();
    if (has_skin_rig && (geometry.skin_joints.size() > 64 || geometry.skin_cluster_joints.size() > 64 ||
        geometry.skin_indices.size() != geometry.positions.size() / 3 * 4 ||
        geometry.skin_weights.size() != geometry.skin_indices.size() || out_joint_entities == nullptr)) return false;

    mesh->vertex_positions.resize(geometry.positions.size() / 3);
    mesh->vertex_normals.resize(geometry.normals.size() / 3);
    // Entity_CreateCube prepares tangent data for its starter geometry. Do not
    // retain any of that mesh's per-vertex streams when replacing the geometry.
    mesh->vertex_tangents.clear();
    mesh->vertex_uvset_0.resize(geometry.uvs.size() / 2);
    mesh->vertex_uvset_1.clear();
    mesh->vertex_boneindices.clear();
    mesh->vertex_boneweights.clear();
    mesh->vertex_boneindices2.clear();
    mesh->vertex_boneweights2.clear();
    mesh->vertex_atlas.clear();
    mesh->vertex_colors.clear();
    mesh->vertex_windweights.clear();
    mesh->morph_targets.clear();
    for (size_t index = 0; index < mesh->vertex_positions.size(); ++index) {
        mesh->vertex_positions[index] = cooked_vector(geometry.positions, index);
        mesh->vertex_normals[index] = cooked_vector(geometry.normals, index);
        mesh->vertex_uvset_0[index] = XMFLOAT2(geometry.uvs[index * 2], geometry.uvs[index * 2 + 1]);
    }
    set_transform(*transform, px, py, pz, qx, qy, qz, qw, sx, sy, sz);
    if (!geometry.tangents.empty()) {
        mesh->vertex_tangents.resize(geometry.tangents.size() / 4);
        for (size_t index = 0; index < mesh->vertex_tangents.size(); ++index) {
            mesh->vertex_tangents[index] = cooked_tangent(geometry.tangents, index);
        }
    }
    assign_cooked_indices(*mesh, geometry.indices);
    std::vector<wi::ecs::Entity> joint_entities;
    JointEntityRollback joint_rollback{scene, joint_entities};
    if (has_skin_rig) {
        mesh->vertex_boneindices.resize(mesh->vertex_positions.size());
        mesh->vertex_boneweights.resize(mesh->vertex_positions.size());
        for (size_t vertex = 0; vertex < mesh->vertex_positions.size(); ++vertex) {
            const size_t offset = vertex * 4;
            for (size_t influence = 0; influence < 4; ++influence) {
                if (geometry.skin_weights[offset + influence] > 0.0f &&
                    geometry.skin_indices[offset + influence] >= geometry.skin_cluster_joints.size()) return false;
            }
            mesh->vertex_boneindices[vertex] = XMUINT4(geometry.skin_indices[offset],
                geometry.skin_indices[offset + 1], geometry.skin_indices[offset + 2], geometry.skin_indices[offset + 3]);
            mesh->vertex_boneweights[vertex] = XMFLOAT4(geometry.skin_weights[offset],
                geometry.skin_weights[offset + 1], geometry.skin_weights[offset + 2], geometry.skin_weights[offset + 3]);
        }
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
            joint_entities.push_back(joint_entity);
        }
        const XMMATRIX armature_inverse = XMMatrixInverse(nullptr, XMLoadFloat4x4(&transform->world));
        for (uint32_t joint_index : geometry.skin_cluster_joints) {
            if (joint_index >= joint_entities.size()) return false;
            const wi::ecs::Entity bone_entity = joint_entities[joint_index];
            const wi::scene::TransformComponent* bone_transform = scene.transforms.GetComponent(bone_entity);
            if (bone_transform == nullptr) return false;
            const XMMATRIX bone_local = XMMatrixMultiply(XMLoadFloat4x4(&bone_transform->world), armature_inverse);
            XMFLOAT4X4 inverse_bind;
            XMStoreFloat4x4(&inverse_bind, XMMatrixInverse(nullptr, bone_local));
            const float* matrix_values = &inverse_bind.m[0][0];
            for (size_t component = 0; component < 16; ++component) {
                if (!std::isfinite(matrix_values[component])) return false;
            }
            armature.boneCollection.push_back(bone_entity);
            armature.inverseBindMatrices.push_back(inverse_bind);
        }
        mesh->armatureID = entity;
    }
    if (mesh->subsets.empty()) return false;
    mesh->subsets[0].indexOffset = 0;
    mesh->subsets[0].indexCount = uint32_t(mesh->indices.size());
    mesh->subsets[0].materialID = entity;
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
