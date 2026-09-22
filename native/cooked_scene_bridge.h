#pragma once

// Installs normalized glTF scene records as Wicked children of their cooked
// mesh root. The cooker owns validation; this bridge owns conversion,
// attachment, and rollback of the backend entities.
#include "coordinate_transform_bridge.h"
#include "coordinate_conventions.h"
#include "cooked_geometry_package.h"
#include "lighting_bridge.h"
#include "wiScene.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace elisa::rendering {

constexpr uint32_t CAMERA_PERSPECTIVE = 0;
constexpr uint32_t CAMERA_ORTHOGRAPHIC = 1;
constexpr uint32_t LIGHT_DIRECTIONAL = 0;
constexpr uint32_t LIGHT_POINT = 1;
constexpr uint32_t LIGHT_SPOT = 2;
constexpr float DEFAULT_IMPORTED_LIGHT_RANGE = 1.0e6f;

struct CookedSceneResources {
    std::vector<wi::ecs::Entity> cameras;
    std::vector<probe::NativeLightHandle> lights;
};

inline bool cooked_scene_matrix(const std::array<float, 12>& source,
    wi::scene::TransformComponent& target) {
    ElisaMatrixPayload payload{};
    for (size_t row = 0; row < 3; ++row) {
        for (size_t column = 0; column < 4; ++column) {
            payload.values_column_major[column * 4 + row] = source[row * 4 + column];
        }
    }
    payload.values_column_major[15] = 1.0f;
    const ElisaCoordinateProfile profile = elisa_coordinate_profile();
    return probe::submit_elisa_matrix(&profile, &payload, &target);
}

inline void discard_cooked_scene(wi::scene::Scene& scene, probe::LightingBridge& lighting,
    CookedSceneResources& resources) {
    for (probe::NativeLightHandle light : resources.lights) lighting.destroy_light(light);
    for (wi::ecs::Entity camera : resources.cameras) scene.Entity_Remove(camera);
    resources.lights.clear();
    resources.cameras.clear();
}

struct CookedSceneRollback {
    wi::scene::Scene& scene;
    probe::LightingBridge& lighting;
    CookedSceneResources& resources;
    bool committed = false;

    ~CookedSceneRollback() {
        if (!committed) discard_cooked_scene(scene, lighting, resources);
    }
};

inline bool install_cooked_scene(wi::scene::Scene& scene, probe::LightingBridge& lighting,
    const elisa::assets::CookedGeometry& geometry, int32_t width, int32_t height,
    wi::ecs::Entity parent, CookedSceneResources& resources) {
    if (width <= 0 || height <= 0 || parent == wi::ecs::INVALID_ENTITY) return false;
    CookedSceneRollback rollback{scene, lighting, resources};
    for (const auto& camera : geometry.cameras) {
        const std::string name = "elisa_imported_camera_" + std::to_string(resources.cameras.size());
        const wi::ecs::Entity entity = scene.Entity_CreateCamera(name, float(width), float(height));
        auto* component = scene.cameras.GetComponent(entity);
        auto* transform = scene.transforms.GetComponent(entity);
        if (entity == wi::ecs::INVALID_ENTITY || component == nullptr || transform == nullptr) {
            if (entity != wi::ecs::INVALID_ENTITY) scene.Entity_Remove(entity);
            discard_cooked_scene(scene, lighting, resources);
            return false;
        }
        bool configured = false;
        if (camera.projection == CAMERA_PERSPECTIVE && camera.x > 0.0f && camera.x < XM_PI &&
            camera.near_clip > 0.0f && camera.far_clip > camera.near_clip) {
            component->CreatePerspective(float(width), float(height), camera.near_clip,
                camera.far_clip, camera.x);
            configured = true;
        } else if (camera.projection == CAMERA_ORTHOGRAPHIC && camera.y > 0.0f &&
            camera.near_clip > 0.0f && camera.far_clip > camera.near_clip) {
            component->CreateOrtho(float(width), float(height), camera.near_clip,
                camera.far_clip, camera.y * 2.0f);
            configured = true;
        }
        if (!configured || !cooked_scene_matrix(camera.transform, *transform)) {
            scene.Entity_Remove(entity);
            discard_cooked_scene(scene, lighting, resources);
            return false;
        }
        scene.Component_Attach(entity, parent, true);
        resources.cameras.push_back(entity);
    }
    for (const auto& light : geometry.lights) {
        probe::NativeLightDesc descriptor;
        descriptor.kind = light.kind == LIGHT_DIRECTIONAL ? probe::NativeLightKind::Directional :
            (light.kind == LIGHT_POINT ? probe::NativeLightKind::Point : probe::NativeLightKind::Spot);
        descriptor.color = XMFLOAT3(light.color[0], light.color[1], light.color[2]);
        descriptor.position = probe::coordinates::to_wicked(light.transform[3], light.transform[7], light.transform[11]);
        const XMFLOAT3 direction = probe::coordinates::to_wicked(-light.transform[2],
            -light.transform[6], -light.transform[10]);
        descriptor.direction = direction;
        descriptor.intensity = light.intensity;
        descriptor.range = light.range > 0.0f ? light.range : DEFAULT_IMPORTED_LIGHT_RANGE;
        descriptor.inner_cone = light.kind == LIGHT_SPOT ? light.inner_cone : 0.0f;
        descriptor.outer_cone = light.kind == LIGHT_SPOT ? light.outer_cone : XM_PIDIV4;
        const probe::NativeLightHandle handle = lighting.create_light(descriptor);
        const wi::ecs::Entity entity = lighting.entity(handle);
        auto* transform = scene.transforms.GetComponent(entity);
        if (handle.owner == 0 || entity == wi::ecs::INVALID_ENTITY || transform == nullptr ||
            !cooked_scene_matrix(light.transform, *transform)) {
            if (handle.owner != 0) lighting.destroy_light(handle);
            discard_cooked_scene(scene, lighting, resources);
            return false;
        }
        scene.Component_Attach(entity, parent, true);
        resources.lights.push_back(handle);
    }
    rollback.committed = true;
    return true;
}

} // namespace elisa::rendering
