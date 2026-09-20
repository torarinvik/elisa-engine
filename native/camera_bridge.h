#pragma once

// Native camera adapter for the backend-neutral camera contract. Viewport and
// projection policy stay explicit; Wicked camera components remain private.
#include "probe_core.h"
#include "wiRenderPath3D.h"
#include "wiScene.h"

#include <cstdint>

namespace probe {

class CameraBridge {
public:
    explicit CameraBridge(wi::scene::Scene& scene) : scene_(scene) {}

    wi::ecs::Entity create_perspective(float width, float height, float scale) {
        if (width <= 0.0f || height <= 0.0f || scale <= 0.0f) return wi::ecs::INVALID_ENTITY;
        const auto entity = scene_.Entity_CreateCamera("elisa_camera_perspective", width * scale, height * scale);
        auto* camera = scene_.cameras.GetComponent(entity);
        if (camera == nullptr) return wi::ecs::INVALID_ENTITY;
        camera->CreatePerspective(width * scale, height * scale, 0.01f, 1000.0f, XM_PIDIV4);
        return entity;
    }

    wi::ecs::Entity create_orthographic(float width, float height, float scale, float vertical_size) {
        if (width <= 0.0f || height <= 0.0f || scale <= 0.0f || vertical_size <= 0.0f) return wi::ecs::INVALID_ENTITY;
        const auto entity = scene_.Entity_CreateCamera("elisa_camera_ortho", width * scale, height * scale);
        auto* camera = scene_.cameras.GetComponent(entity);
        if (camera == nullptr) return wi::ecs::INVALID_ENTITY;
        camera->CreateOrtho(width * scale, height * scale, 0.01f, 1000.0f, vertical_size);
        return entity;
    }

    bool resize(wi::ecs::Entity entity, float width, float height, float scale) {
        auto* camera = scene_.cameras.GetComponent(entity);
        if (camera == nullptr || width <= 0.0f || height <= 0.0f || scale <= 0.0f) return false;
        if ((camera->_flags & wi::scene::CameraComponent::ORTHO) != 0) {
            camera->CreateOrtho(width * scale, height * scale, camera->zNearP, camera->zFarP,
                camera->ortho_vertical_size);
        } else {
            camera->CreatePerspective(width * scale, height * scale, camera->zNearP, camera->zFarP, camera->fov);
        }
        return true;
    }

    bool activate(wi::RenderPath3D& path, wi::ecs::Entity entity) {
        auto* camera = scene_.cameras.GetComponent(entity);
        if (camera == nullptr) return false;
        path.scene = &scene_;
        path.camera = camera;
        return true;
    }

private:
    wi::scene::Scene& scene_;
};

inline bool probe_camera_bridge(wi::scene::Scene& scene, wi::RenderPath3D& path, wi::ecs::Entity original_entity) {
    CameraBridge bridge(scene);
    const auto perspective = bridge.create_perspective(320.0f, 200.0f, 2.0f);
    const auto ortho = bridge.create_orthographic(320.0f, 200.0f, 2.0f, 8.0f);
    auto* perspective_component = scene.cameras.GetComponent(perspective);
    auto* ortho_component = scene.cameras.GetComponent(ortho);
    if (!check(perspective_component != nullptr && ortho_component != nullptr,
        "camera bridge creates both projections") ||
        !check(perspective_component->width == 640.0f && perspective_component->height == 400.0f,
            "camera bridge applies high-DPI viewport") ||
        !check((ortho_component->_flags & wi::scene::CameraComponent::ORTHO) != 0,
            "camera bridge selects orthographic projection") ||
        !check(bridge.resize(perspective, 640.0f, 400.0f, 1.0f),
            "camera bridge resizes perspective view") ||
        !check(bridge.activate(path, ortho) && path.camera == ortho_component && path.scene == &scene,
            "camera bridge switches to orthographic view") ||
        !check(bridge.activate(path, perspective) && path.camera == perspective_component,
            "camera bridge switches back to perspective view") ||
        !check(!bridge.activate(path, wi::ecs::INVALID_ENTITY) && path.camera == perspective_component,
            "camera bridge rejects missing view without changing active camera")) return false;
    path.camera = scene.cameras.GetComponent(original_entity);
    scene.Entity_Remove(perspective);
    scene.Entity_Remove(ortho);
    return check(scene.cameras.GetComponent(perspective) == nullptr && scene.cameras.GetComponent(ortho) == nullptr,
        "camera bridge releases view entities");
}

} // namespace probe
