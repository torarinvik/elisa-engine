#pragma once
// Wicked-dependent helpers for the native scene probe: the right-handed to
// Wicked-space conversion and cell markers. The Wicked-free helpers (check,
// manifest parsing, the generated test WAV) live in probe_core.h so the
// sanitizer boundary harness can share them without a renderer header.
#include "wiScene.h"
#include "probe_core.h"

namespace probe {
// Elisa math is right-handed with +Y up and -Z forward (see
// src/math/geometry.elisa), and so is the Godot host. Wicked is a
// left-handed renderer, so its screen-right is Elisa's +X. Negating X at
// this boundary is the RH->LH conversion that keeps both hosts showing the
// same world instead of a mirrored one. Verified by sampling projected
// maze cells in both frames: without this the native image is the mirror
// of the Godot image.
XMFLOAT3 to_wicked_space(float x, float y, float z) {
    return XMFLOAT3(-x, y, z);
}

// A small unlit cube marking one game cell: player-facing evidence that the
// host draws the Elisa game's objects (key, door, hazards, goal), not just
// walls. Same RH->LH conversion as every other position.
wi::ecs::Entity create_cell_marker(wi::scene::Scene& scene, const std::string& name,
    int cell_x, int cell_y, float red, float green, float blue) {
    const auto entity = scene.Entity_CreateCube(name);
    if (entity == wi::ecs::INVALID_ENTITY) {
        return entity;
    }
    auto* transform = scene.transforms.GetComponent(entity);
    auto* material = scene.materials.GetComponent(entity);
    if (transform == nullptr) {
        return wi::ecs::INVALID_ENTITY;
    }
    transform->translation_local = to_wicked_space(
        (float)cell_x * 0.6f - 2.1f, (float)cell_y * 0.6f - 2.1f, 1.0f);
    transform->scale_local = XMFLOAT3(0.26f, 0.26f, 0.26f);
    transform->UpdateTransform();
    if (material != nullptr) {
        material->shaderType = wi::scene::MaterialComponent::SHADERTYPE_UNLIT;
        material->baseColor = XMFLOAT4(red, green, blue, 1.0f);
    }
    return entity;
}
}
