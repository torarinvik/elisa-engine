#pragma once

// Cold and warm Wicked shader-path probe. These helpers are isolated from the
// regular gameplay smoke so cache behavior can be measured independently.
#include "camera_bridge.h"
#include "native_application.h"
#include "probe_support.h"
#include "wiJobSystem.h"
#include "wiRenderPath3D.h"
#include "wiRenderer.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace probe {

inline void configure_probe_shader_paths(const std::string& wicked_root) {
    const std::string source_root = wicked_root + "/shaders/";
    const char* configured_output = std::getenv("ELISA_PROBE_SHADER_ROOT");
    std::string output_root = configured_output != nullptr && configured_output[0] != '\0'
        ? configured_output : source_root;
    if (!output_root.empty() && output_root.back() != '/') output_root.push_back('/');
    wi::renderer::SetShaderPath(output_root);
    wi::renderer::SetShaderSourcePath(source_root);
}

inline int run_shader_warmup_probe(NativeApplication& host, wi::Application& application,
    wi::scene::Scene& scene, wi::ecs::Entity object, wi::ecs::Entity camera,
    const std::map<std::string, std::string>& manifest) {
    const float object_x = std::stof(manifest.at("object_x"));
    const float object_y = std::stof(manifest.at("object_y"));
    const float object_z = std::stof(manifest.at("object_z"));
    const float camera_x = std::stof(manifest.at("camera_x"));
    const float camera_y = std::stof(manifest.at("camera_y"));
    const float camera_z = std::stof(manifest.at("camera_z"));
    auto* object_transform = scene.transforms.GetComponent(object);
    auto* object_material = scene.materials.GetComponent(object);
    auto* camera_transform = scene.transforms.GetComponent(camera);
    auto* camera_component = scene.cameras.GetComponent(camera);
    if (!check(object_transform != nullptr && object_material != nullptr &&
        camera_transform != nullptr && camera_component != nullptr,
        "shader warm-up scene components")) return 1;

    object_material->shaderType = wi::scene::MaterialComponent::SHADERTYPE_UNLIT;
    object_material->baseColor = XMFLOAT4(0.2f, 0.7f, 1.0f, 1.0f);
    object_transform->translation_local = to_wicked_space(object_x, object_y, object_z);
    object_transform->scale_local = XMFLOAT3(0.3f, 0.3f, 0.3f);
    object_transform->UpdateTransform();
    camera_transform->translation_local = to_wicked_space(camera_x, camera_y, camera_z);
    camera_transform->UpdateTransform();
    camera_component->TransformCamera(*camera_transform);
    camera_component->UpdateCamera();

    wi::RenderPath3D render_path;
    render_path.scene = &scene;
    render_path.camera = camera_component;
    if (!probe_camera_bridge(scene, render_path, camera)) return 1;
    render_path.camera = scene.cameras.GetComponent(camera);
    application.ActivatePath(&render_path);

    constexpr int total_frames = 9;
    std::vector<int64_t> frame_micros;
    frame_micros.reserve(total_frames - 1);
    int64_t first_frame_micros = 0;
    for (int frame = 0; frame < total_frames; ++frame) {
        if (!host.poll_events()) {
            host.shutdown();
            return 1;
        }
        const auto frame_start = std::chrono::steady_clock::now();
        host.run_frame();
        wi::jobsystem::WaitForAllJobs();
        wi::graphics::GetDevice()->WaitForGPU();
        const int64_t elapsed_micros = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - frame_start).count();
        if (frame == 0) {
            first_frame_micros = elapsed_micros;
        } else {
            frame_micros.push_back(elapsed_micros);
        }
    }
    std::sort(frame_micros.begin(), frame_micros.end());
    std::fprintf(stdout,
        "shader warm-up: frames=%d first_frame_us=%lld warm_median_us=%lld warm_p95_us=%lld\n",
        total_frames, (long long)first_frame_micros,
        (long long)frame_micros[frame_micros.size() / 2],
        (long long)frame_micros[(frame_micros.size() * 95) / 100]);
    host.shutdown();
    return 0;
}

} // namespace probe
