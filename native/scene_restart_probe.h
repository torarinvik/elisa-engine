#pragma once

#include "native_application.h"
#include "probe_support.h"
#include "wiGraphicsDevice.h"
#include "wiLua.h"
#include "wiRenderer.h"
#include "wiRenderPath3D.h"
#include "wiScene.h"
#include "Foundation/Foundation.hpp"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <malloc/malloc.h>
#include <thread>
#include <vector>

namespace probe {

inline size_t scene_restart_heap_bytes_in_use() {
    malloc_statistics_t stats{};
    malloc_zone_statistics(malloc_default_zone(), &stats);
    return stats.size_in_use;
}

inline void hold_scene_restart_inspection(const char* environment_name, const char* description) {
    const char* configured_seconds = std::getenv(environment_name);
    if (configured_seconds == nullptr) return;
    const int seconds = std::atoi(configured_seconds);
    if (seconds <= 0) return;
    std::fprintf(stdout, "%s: %d seconds\n", description, seconds);
    std::fflush(stdout);
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
}

inline bool read_scene_restart_count(const char* environment_name, size_t fallback,
        size_t maximum, size_t& output) {
    const char* configured = std::getenv(environment_name);
    if (configured == nullptr) {
        output = fallback;
        return true;
    }
    errno = 0;
    char* end = nullptr;
    const unsigned long long value = std::strtoull(configured, &end, 10);
    if (errno != 0 || end == configured || *end != '\0' || value == 0 || value > maximum ||
        value > std::numeric_limits<size_t>::max()) {
        std::fprintf(stderr, "%s must be an integer between 1 and %zu\n",
            environment_name, maximum);
        return false;
    }
    output = size_t(value);
    return true;
}

inline bool wait_for_scene_restart_pipeline_idle() {
    constexpr size_t PIPELINE_WAIT_ATTEMPTS = 1000;
    constexpr size_t PIPELINE_IDLE_CONFIRMATIONS = 3;
    constexpr auto PIPELINE_WAIT_INTERVAL = std::chrono::milliseconds(10);
    size_t idle_confirmations = 0;
    for (size_t attempt = 0; attempt < PIPELINE_WAIT_ATTEMPTS; ++attempt) {
        if (wi::renderer::IsPipelineCreationActive() == 0) {
            if (++idle_confirmations == PIPELINE_IDLE_CONFIRMATIONS) return true;
        } else {
            idle_confirmations = 0;
        }
        std::this_thread::sleep_for(PIPELINE_WAIT_INTERVAL);
    }
    return false;
}

inline bool probe_lua_timer_wakeup_allocations() {
    static constexpr char SCRIPT[] = R"lua(
local TIMER_WAIT_SECONDS = 1000000
local TIMER_UPDATE_SECONDS = 0.001
local IDLE_TIMER_UPDATES = 1000
local MAX_IDLE_HEAP_GROWTH_KIB = 1.0
local resumed = 0
local timer_thread = coroutine.create(function()
    waitSeconds(TIMER_WAIT_SECONDS)
    resumed = resumed + 1
end)
assert(coroutine.resume(timer_thread))
local gc_was_running = collectgarbage("isrunning")
collectgarbage("stop")
local heap_before = collectgarbage("count")
for index = 1, IDLE_TIMER_UPDATES do
    wakeUpWaitingThreads(TIMER_UPDATE_SECONDS)
end
local heap_after = collectgarbage("count")
local no_frame_allocation = heap_after - heap_before < MAX_IDLE_HEAP_GROWTH_KIB
if gc_was_running then collectgarbage("restart") end
assert(no_frame_allocation, "idle timer updates allocated temporary tables")
wakeUpWaitingThreads(TIMER_WAIT_SECONDS)
assert(resumed == 1, "expired timer coroutine did not resume")
)lua";
    return check(wi::lua::RunText(SCRIPT),
        "Lua timer waits resume and idle frame updates avoid temporary tables");
}

inline bool probe_in_process_scene_restarts(NativeApplication& host) {
    auto* device = wi::graphics::GetDevice();
    if (!check(device != nullptr, "scene restart graphics device")) return false;
    if (!probe_lua_timer_wakeup_allocations()) return false;

    wi::Application& application = host.wicked();
    application.ActivatePath(nullptr);
    device->WaitForGPU();

    constexpr size_t DEFAULT_RESTART_CYCLES = 64;
    constexpr size_t DEFAULT_WARMUP_CYCLES = 6;
    constexpr size_t MAX_RESTART_CYCLES = 4096;
    constexpr size_t RESTART_RENDER_FRAMES = 2;
    size_t restart_cycles = 0;
    size_t warmup_cycles = 0;
    if (!read_scene_restart_count("ELISA_SCENE_RESTART_CYCLES", DEFAULT_RESTART_CYCLES,
            MAX_RESTART_CYCLES, restart_cycles) ||
        !read_scene_restart_count("ELISA_SCENE_RESTART_WARMUP_CYCLES", DEFAULT_WARMUP_CYCLES,
            MAX_RESTART_CYCLES, warmup_cycles) ||
        warmup_cycles >= restart_cycles) {
        std::fprintf(stderr, "scene restart warm-up must be less than the total cycle count\n");
        return false;
    }
    std::vector<uint64_t> gpu_samples(restart_cycles);
    std::vector<size_t> heap_samples(restart_cycles);
    uint64_t warm_gpu_bytes = 0;
    uint64_t final_gpu_bytes = 0;
    size_t warm_heap_bytes = 0;
    size_t final_heap_bytes = 0;
    for (size_t cycle = 0; cycle < restart_cycles; ++cycle) {
        bool cycle_ok = false;
        {
            NS::SharedPtr<NS::AutoreleasePool> autorelease_pool =
                NS::TransferPtr(NS::AutoreleasePool::alloc()->init());
            {
                wi::scene::Scene scene;
                const auto cube = scene.Entity_CreateCube("scene_restart_cube");
                const auto camera = scene.Entity_CreateCamera("scene_restart_camera", 320, 200);
                const auto lamp = scene.Entity_CreateLight("scene_restart_light");
                auto* cube_transform = scene.transforms.GetComponent(cube);
                auto* cube_material = scene.materials.GetComponent(cube);
                auto* camera_transform = scene.transforms.GetComponent(camera);
                auto* camera_component = scene.cameras.GetComponent(camera);
                bool entities_ready = cube != wi::ecs::INVALID_ENTITY &&
                    camera != wi::ecs::INVALID_ENTITY && lamp != wi::ecs::INVALID_ENTITY &&
                    cube_transform != nullptr && cube_material != nullptr &&
                    camera_transform != nullptr && camera_component != nullptr;
                if (entities_ready) {
                    cube_transform->translation_local = XMFLOAT3(0, 0, 1);
                    cube_transform->scale_local = XMFLOAT3(0.5f, 0.5f, 0.5f);
                    cube_transform->UpdateTransform();
                    cube_material->shaderType = wi::scene::MaterialComponent::SHADERTYPE_UNLIT;
                    cube_material->baseColor = XMFLOAT4(0.2f, 0.7f, 1.0f, 1.0f);
                    camera_transform->translation_local = XMFLOAT3(0, 0, -5);
                    camera_transform->UpdateTransform();
                    camera_component->TransformCamera(*camera_transform);
                    camera_component->UpdateCamera();

                    wi::RenderPath3D render_path;
                    render_path.scene = &scene;
                    render_path.camera = camera_component;
                    render_path.setOcclusionCullingEnabled(false);
                    application.ActivatePath(&render_path);
                    bool frames_rendered = true;
                    for (size_t frame = 0; frame < RESTART_RENDER_FRAMES; ++frame) {
                        if (!host.run_frame()) {
                            frames_rendered = false;
                            break;
                        }
                    }
                    device->WaitForGPU();
                    cycle_ok = check(frames_rendered && render_path.GetRenderResult3D().IsValid(),
                        "restarted scene renders before teardown");
                    application.ActivatePath(nullptr);
                    device->WaitForGPU();
                }

                scene.Clear();
                cycle_ok = check(entities_ready && scene.objects.GetCount() == 0 &&
                    scene.meshes.GetCount() == 0 && scene.transforms.GetCount() == 0 &&
                    scene.materials.GetCount() == 0 && scene.cameras.GetCount() == 0 &&
                    scene.lights.GetCount() == 0, "restarted scene clears owned components") && cycle_ok;
            }
        }
        if (!cycle_ok) {
            application.ActivatePath(nullptr);
            device->WaitForGPU();
            return false;
        }
        device->WaitForGPU();
        if (!check(wait_for_scene_restart_pipeline_idle(),
                "Wicked pipeline creation drains between scene restarts")) {
            return false;
        }
        const uint64_t gpu_bytes = device->GetMemoryUsage().usage;
        const size_t heap_bytes = scene_restart_heap_bytes_in_use();
        gpu_samples[cycle] = gpu_bytes;
        heap_samples[cycle] = heap_bytes;
        if (cycle + 1 == warmup_cycles) {
            warm_gpu_bytes = gpu_bytes;
            warm_heap_bytes = heap_bytes;
            hold_scene_restart_inspection(
                "ELISA_SCENE_RESTART_WARMUP_HOLD_SECONDS", "scene restart warm-up inspection hold");
        }
        final_gpu_bytes = gpu_bytes;
        final_heap_bytes = heap_bytes;
    }

    const long long heap_delta = static_cast<long long>(final_heap_bytes) -
        static_cast<long long>(warm_heap_bytes);
    const long long gpu_delta = static_cast<long long>(final_gpu_bytes) -
        static_cast<long long>(warm_gpu_bytes);
    std::fprintf(stdout,
        "in-process scene restart: cycles=%zu warmup_cycles=%zu measured_cycles=%zu rendered=1 components_cleared=1 gpu_delta_bytes=%lld heap_delta_bytes=%lld\n",
        restart_cycles, warmup_cycles, restart_cycles - warmup_cycles, gpu_delta, heap_delta);
    std::fprintf(stdout, "scene restart GPU usage samples:");
    for (const uint64_t sample : gpu_samples) std::fprintf(stdout, " %llu", (unsigned long long)sample);
    std::fprintf(stdout, "\nscene restart heap usage samples:");
    for (const size_t sample : heap_samples) std::fprintf(stdout, " %zu", sample);
    std::fprintf(stdout, "\n");
    return check(application.GetActivePath() == nullptr,
        "scene restart leaves no active path referencing destroyed scene");
}

inline bool probe_scene_despawn(wi::scene::Scene& scene, wi::ecs::Entity object,
        wi::ecs::Entity camera, wi::ecs::Entity lamp, wi::ecs::Entity physics_box,
        const std::vector<wi::ecs::Entity>& walls,
        const std::vector<wi::ecs::Entity>& markers) {
    scene.Entity_Remove(object);
    scene.Entity_Remove(camera);
    scene.Entity_Remove(lamp);
    for (const auto& wall : walls) scene.Entity_Remove(wall);
    for (const auto& marker : markers) scene.Entity_Remove(marker);
    scene.Entity_Remove(physics_box);
    if (!check(scene.objects.GetComponent(object) == nullptr, "cube despawn") ||
        !check(scene.meshes.GetComponent(object) == nullptr, "mesh despawn") ||
        !check(scene.cameras.GetComponent(camera) == nullptr, "camera despawn") ||
        !check(scene.lights.GetComponent(lamp) == nullptr, "lamp despawn") ||
        (!walls.empty() &&
            !check(scene.objects.GetComponent(walls.front()) == nullptr, "wall despawn"))) {
        return false;
    }
    std::fprintf(stdout, "scene despawn passed\n");
    return true;
}

inline int run_scene_restart_diagnostic(NativeApplication& host) {
    const bool restart_ok = probe_in_process_scene_restarts(host);
    hold_scene_restart_inspection(
        "ELISA_SCENE_RESTART_HOLD_SECONDS", "scene restart final inspection hold");
    host.shutdown();
    return restart_ok ? 0 : 1;
}

} // namespace probe
