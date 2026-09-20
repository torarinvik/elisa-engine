#pragma once

#include "cooked_geometry_package.h"
#include "wiScene.h"

#include <cmath>
#include <cstdint>
#include <limits>

namespace elisa::rendering {

inline void set_transform(wi::scene::TransformComponent& transform,
    float px, float py, float pz,
    float qx, float qy, float qz, float qw,
    float sx, float sy, float sz) {
    const double length = std::sqrt(double(qx) * qx + double(qy) * qy +
        double(qz) * qz + double(qw) * qw);
    const float inverse_length = float(1.0 / length);
    transform.translation_local = XMFLOAT3(px, py, pz);
    transform.rotation_local = XMFLOAT4(qx * inverse_length, qy * inverse_length,
        qz * inverse_length, qw * inverse_length);
    transform.scale_local = XMFLOAT3(sx, sy, sz);
    transform.SetDirty();
    transform.UpdateTransform();
}

inline bool configure_cooked_mesh(wi::scene::Scene& scene, wi::ecs::Entity entity,
    const elisa::assets::CookedGeometry& geometry,
    float px, float py, float pz, float qx, float qy, float qz, float qw,
    float sx, float sy, float sz, float red, float green, float blue, float alpha) {
    wi::scene::TransformComponent* transform = scene.transforms.GetComponent(entity);
    wi::scene::MaterialComponent* material = scene.materials.GetComponent(entity);
    wi::scene::ObjectComponent* object = scene.objects.GetComponent(entity);
    wi::scene::MeshComponent* mesh = scene.meshes.GetComponent(entity);
    if (transform == nullptr || material == nullptr || object == nullptr || mesh == nullptr ||
        geometry.positions.size() % 3 != 0 || geometry.normals.size() != geometry.positions.size() ||
        geometry.uvs.size() != geometry.positions.size() / 3 * 2 || geometry.indices.empty() ||
        geometry.indices.size() > std::numeric_limits<uint32_t>::max()) return false;

    mesh->vertex_positions.resize(geometry.positions.size() / 3);
    mesh->vertex_normals.resize(geometry.normals.size() / 3);
    mesh->vertex_uvset_0.resize(geometry.uvs.size() / 2);
    for (size_t index = 0; index < mesh->vertex_positions.size(); ++index) {
        mesh->vertex_positions[index] = XMFLOAT3(geometry.positions[index * 3],
            geometry.positions[index * 3 + 1], geometry.positions[index * 3 + 2]);
        mesh->vertex_normals[index] = XMFLOAT3(geometry.normals[index * 3],
            geometry.normals[index * 3 + 1], geometry.normals[index * 3 + 2]);
        mesh->vertex_uvset_0[index] = XMFLOAT2(geometry.uvs[index * 2], geometry.uvs[index * 2 + 1]);
    }
    mesh->indices.assign(geometry.indices.begin(), geometry.indices.end());
    if (mesh->subsets.empty()) return false;
    mesh->subsets[0].indexOffset = 0;
    mesh->subsets[0].indexCount = uint32_t(mesh->indices.size());
    mesh->CreateRenderData();
    set_transform(*transform, px, py, pz, qx, qy, qz, qw, sx, sy, sz);
    material->shaderType = wi::scene::MaterialComponent::SHADERTYPE_UNLIT;
    material->SetBaseColor(XMFLOAT4(red, green, blue, alpha));
    material->SetCastShadow(false);
    material->userBlendMode = alpha < 0.999f ? wi::enums::BLENDMODE_ALPHA : wi::enums::BLENDMODE_OPAQUE;
    object->SetCastShadow(false);
    return true;
}

} // namespace elisa::rendering
