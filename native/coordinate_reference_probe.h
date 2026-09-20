#pragma once

#include "coordinate_abi.h"
#include "coordinate_transform_bridge.h"
#include "png_capture.h"
#include "probe_core.h"
#include "wiApplication.h"
#include "wiGraphics.h"
#include "wiScene.h"

#include <filesystem>
#include <string>

namespace probe {

inline bool probe_coordinate_reference(wi::Application& application, wi::scene::Scene& scene,
    const char* screenshot_path) {
    const ElisaCoordinateProfile profile = elisa_coordinate_profile();
    const ElisaTransformPayload transforms[] = {
        {{0.0f, 0.0f, 0.7f}, {0.0f, 0.0f, 0.258819f, 0.965926f}, {-0.8f, 0.22f, 0.12f}},
        {{1.25f, -0.45f, 0.7f}, {0.0f, 0.0f, 0.0f, 1.0f}, {0.16f, 0.16f, 0.16f}},
        {{-0.95f, 0.85f, 0.7f}, {0.0f, 0.0f, 0.0f, 1.0f}, {0.16f, 0.16f, 0.16f}},
        {{-0.7f, -0.9f, 0.7f}, {0.0f, 0.0f, 0.0f, 1.0f}, {0.16f, 0.16f, 0.16f}},
    };
    const XMFLOAT4 colors[] = {
        XMFLOAT4(1.0f, 0.78f, 0.12f, 1.0f), XMFLOAT4(0.95f, 0.18f, 0.12f, 1.0f),
        XMFLOAT4(0.12f, 0.85f, 0.28f, 1.0f), XMFLOAT4(0.15f, 0.45f, 1.0f, 1.0f),
    };
    wi::ecs::Entity entities[4]{};
    for (size_t index = 0; index < 4; ++index) {
        entities[index] = scene.Entity_CreateCube("elisa_coordinate_reference_" + std::to_string(index));
        auto* transform = scene.transforms.GetComponent(entities[index]);
        auto* material = scene.materials.GetComponent(entities[index]);
        if (!check(entities[index] != wi::ecs::INVALID_ENTITY && transform != nullptr && material != nullptr,
                "coordinate reference entity components") ||
            !check(submit_elisa_transform(&profile, &transforms[index], transform),
                "coordinate reference transform submission")) {
            return false;
        }
        transform->UpdateTransform();
        material->shaderType = wi::scene::MaterialComponent::SHADERTYPE_UNLIT;
        material->baseColor = colors[index];
    }
    for (int frame = 0; frame < 4; ++frame) {
        application.Run();
    }
    wi::graphics::GetDevice()->WaitForGPU();
    const wi::graphics::Texture presented = wi::graphics::GetDevice()->GetBackBuffer(&application.swapChain);
    const std::filesystem::path source(screenshot_path);
    const std::string reference_path =
        (source.parent_path() / (source.stem().string() + "-coordinates" + source.extension().string())).string();
    const bool captured = check(presented.IsValid() && save_rgba_png(presented, reference_path),
        "coordinate reference frame capture");
    for (const wi::ecs::Entity entity : entities) {
        scene.Entity_Remove(entity);
    }
    if (captured) {
        std::fprintf(stdout, "coordinate reference screenshot saved: %s\n", reference_path.c_str());
    }
    return captured;
}

} // namespace probe
