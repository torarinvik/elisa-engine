#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <vector>

namespace {

std::array<bool, MAX_INSTANCES> lod_probe_saved_renderable{};
std::array<bool, MAX_OVERLAY_TEXTS> lod_probe_saved_text_hidden{};
std::array<bool, MAX_OVERLAY_PANELS> lod_probe_saved_panel_hidden{};
std::array<bool, MAX_OVERLAY_IMAGES> lod_probe_saved_image_hidden{};
bool lod_probe_isolated = false;

struct LodGpuTimingState {
    wi::graphics::GPUQueryHeap heap;
    wi::graphics::GPUBuffer readback;
    uint32_t query_count = 0;
    uint32_t next_query = 0;
    uint32_t open_begin = UINT32_MAX;
    bool capturing = false;
    std::vector<std::pair<uint32_t, uint32_t>> ranges;
};

LodGpuTimingState lod_gpu_timing;

} // namespace

namespace {

void lod_gpu_timing_begin_render() {
    if (!lod_gpu_timing.capturing || lod_gpu_timing.next_query >= lod_gpu_timing.query_count) return;
    wi::graphics::GraphicsDevice* device = wi::graphics::GetDevice();
    if (device == nullptr) return;
    lod_gpu_timing.open_begin = lod_gpu_timing.next_query++;
    const wi::graphics::CommandList command_list = device->BeginCommandList();
    device->QueryEnd(&lod_gpu_timing.heap, lod_gpu_timing.open_begin, command_list);
}

void lod_gpu_timing_end_render() {
    if (!lod_gpu_timing.capturing || lod_gpu_timing.open_begin == UINT32_MAX ||
        lod_gpu_timing.next_query >= lod_gpu_timing.query_count) return;
    wi::graphics::GraphicsDevice* device = wi::graphics::GetDevice();
    if (device == nullptr) return;
    const uint32_t end = lod_gpu_timing.next_query++;
    const wi::graphics::CommandList command_list = device->BeginCommandList();
    device->QueryEnd(&lod_gpu_timing.heap, end, command_list);
    lod_gpu_timing.ranges.emplace_back(lod_gpu_timing.open_begin, end);
    lod_gpu_timing.open_begin = UINT32_MAX;
}

} // namespace

extern "C" int32_t elisa_render_scene_v1_test_wait_render_pipelines(void) {
    constexpr uint32_t PIPELINE_WAIT_ATTEMPTS = 3000;
    constexpr float PIPELINE_WAIT_MILLISECONDS = 10.0f;
    for (uint32_t attempt = 0; attempt < PIPELINE_WAIT_ATTEMPTS; ++attempt) {
        if (wi::renderer::IsPipelineCreationActive() == 0) return ELISA_RENDER_SCENE_OK;
        wi::helper::Sleep(PIPELINE_WAIT_MILLISECONDS);
    }
    return ELISA_RENDER_SCENE_BACKEND_FAILED;
}

// Hide unrelated scene instances while capturing same-camera LOD references.
extern "C" int32_t elisa_render_scene_v1_test_isolate_snapshot_instance(
        int64_t handle, int32_t isolate) {
    if (isolate != 0 && isolate != 1) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.scene == nullptr) {
        return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    }
    if (isolate != 0) {
        if (lod_probe_isolated) return ELISA_RENDER_SCENE_BACKEND_FAILED;
        size_t target_slot = MAX_INSTANCES;
        if (!valid_handle(state, handle, target_slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
        for (size_t slot = 0; slot < MAX_INSTANCES; ++slot) {
            const InstanceSlot& instance = state.instances[slot];
            if (!instance.live) continue;
            wi::scene::ObjectComponent* object = state.scene->objects.GetComponent(instance.entity);
            if (object == nullptr) return ELISA_RENDER_SCENE_BACKEND_FAILED;
            lod_probe_saved_renderable[slot] = object->IsRenderable();
        }
        for (size_t slot = 0; slot < MAX_INSTANCES; ++slot) {
            const InstanceSlot& instance = state.instances[slot];
            if (!instance.live) continue;
            wi::scene::ObjectComponent* object = state.scene->objects.GetComponent(instance.entity);
            object->SetRenderable(slot == target_slot);
        }
        for (size_t slot = 0; slot < MAX_OVERLAY_TEXTS; ++slot) {
            OverlayTextSlot& text = state.overlay_texts[slot];
            lod_probe_saved_text_hidden[slot] = text.font.IsHidden();
            if (text.live) text.font.SetHidden(true);
        }
        for (size_t slot = 0; slot < MAX_OVERLAY_PANELS; ++slot) {
            OverlayPanelSlot& panel = state.overlay_panels[slot];
            lod_probe_saved_panel_hidden[slot] = panel.sprite.IsHidden();
            if (panel.live) panel.sprite.SetHidden(true);
        }
        for (size_t slot = 0; slot < MAX_OVERLAY_IMAGES; ++slot) {
            OverlayImageSlot& image = state.overlay_images[slot];
            lod_probe_saved_image_hidden[slot] = image.sprite.IsHidden();
            if (image.live) image.sprite.SetHidden(true);
        }
        lod_probe_isolated = true;
        return ELISA_RENDER_SCENE_OK;
    }
    if (!lod_probe_isolated) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    for (size_t slot = 0; slot < MAX_INSTANCES; ++slot) {
        const InstanceSlot& instance = state.instances[slot];
        if (!instance.live) continue;
        wi::scene::ObjectComponent* object = state.scene->objects.GetComponent(instance.entity);
        if (object == nullptr) return ELISA_RENDER_SCENE_BACKEND_FAILED;
        object->SetRenderable(lod_probe_saved_renderable[slot]);
    }
    for (size_t slot = 0; slot < MAX_OVERLAY_TEXTS; ++slot) {
        state.overlay_texts[slot].font.SetHidden(lod_probe_saved_text_hidden[slot]);
    }
    for (size_t slot = 0; slot < MAX_OVERLAY_PANELS; ++slot) {
        state.overlay_panels[slot].sprite.SetHidden(lod_probe_saved_panel_hidden[slot]);
    }
    for (size_t slot = 0; slot < MAX_OVERLAY_IMAGES; ++slot) {
        state.overlay_images[slot].sprite.SetHidden(lod_probe_saved_image_hidden[slot]);
    }
    lod_probe_saved_renderable = {};
    lod_probe_saved_text_hidden = {};
    lod_probe_saved_panel_hidden = {};
    lod_probe_saved_image_hidden = {};
    lod_probe_isolated = false;
    return ELISA_RENDER_SCENE_OK;
}

// Measure CPU submission plus completed GPU frame latency for an already
// selected LOD. This intentionally reports end-to-end frame time, not GPU-only
// time; Wicked's renderer owns the command lists needed for GPU timestamp ranges.
extern "C" int32_t elisa_render_scene_v1_test_lod_frame_times(
        int64_t handle, uint32_t expected_level, uint32_t warmup_frames,
        uint32_t sample_count, uint64_t* median_microseconds,
        uint64_t* p95_microseconds) {
    constexpr uint32_t MAX_WARMUP_FRAMES = 60;
    constexpr uint32_t MIN_SAMPLE_COUNT = 5;
    constexpr uint32_t MAX_SAMPLE_COUNT = 120;
    if (warmup_frames == 0 || warmup_frames > MAX_WARMUP_FRAMES ||
        sample_count < MIN_SAMPLE_COUNT || sample_count > MAX_SAMPLE_COUNT ||
        median_microseconds == nullptr || p95_microseconds == nullptr) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    {
        RenderSceneService& state = service();
        std::lock_guard<std::mutex> guard(state.mutex);
        size_t slot = MAX_INSTANCES;
        if (!state.initialized || !valid_handle(state, handle, slot)) {
            return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
        }
        if (state.instances[slot].snapshot_lod_level != expected_level) {
            return ELISA_RENDER_SCENE_BACKEND_FAILED;
        }
    }
    wi::graphics::GraphicsDevice* device = wi::graphics::GetDevice();
    if (device == nullptr) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    if (lod_gpu_timing.capturing) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    lod_gpu_timing = LodGpuTimingState{};
    lod_gpu_timing.query_count = sample_count * 2;
    wi::graphics::GPUQueryHeapDesc query_desc;
    query_desc.type = wi::graphics::GpuQueryType::TIMESTAMP;
    query_desc.query_count = lod_gpu_timing.query_count;
    if (!device->CreateQueryHeap(&query_desc, &lod_gpu_timing.heap)) {
        lod_gpu_timing = LodGpuTimingState{};
        return ELISA_RENDER_SCENE_BACKEND_FAILED;
    }
    wi::graphics::GPUBufferDesc readback_desc;
    readback_desc.usage = wi::graphics::Usage::READBACK;
    readback_desc.size = uint64_t(lod_gpu_timing.query_count) * sizeof(uint64_t);
    if (!device->CreateBuffer(&readback_desc, nullptr, &lod_gpu_timing.readback)) {
        lod_gpu_timing = LodGpuTimingState{};
        return ELISA_RENDER_SCENE_BACKEND_FAILED;
    }
    wi::graphics::CommandList reset_command_list = device->BeginCommandList();
    device->QueryReset(&lod_gpu_timing.heap, 0, lod_gpu_timing.query_count, reset_command_list);
    device->SubmitCommandLists();
    device->WaitForGPU();
    for (uint32_t frame = 0; frame < warmup_frames; ++frame) {
        if (elisa_application_v1_pump() != ELISA_APPLICATION_RUNNING) {
            lod_gpu_timing = LodGpuTimingState{};
            return ELISA_RENDER_SCENE_BACKEND_FAILED;
        }
        device->WaitForGPU();
    }
    std::vector<uint64_t> samples;
    samples.reserve(sample_count);
    lod_gpu_timing.capturing = true;
    for (uint32_t frame = 0; frame < sample_count; ++frame) {
        const auto started = std::chrono::steady_clock::now();
        if (elisa_application_v1_pump() != ELISA_APPLICATION_RUNNING) {
            lod_gpu_timing.capturing = false;
            lod_gpu_timing = LodGpuTimingState{};
            return ELISA_RENDER_SCENE_BACKEND_FAILED;
        }
        device->WaitForGPU();
        const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count();
        samples.push_back(static_cast<uint64_t>(std::max<int64_t>(elapsed, 0)));
    }
    lod_gpu_timing.capturing = false;
    if (lod_gpu_timing.ranges.size() != sample_count || lod_gpu_timing.open_begin != UINT32_MAX) {
        lod_gpu_timing = LodGpuTimingState{};
        return ELISA_RENDER_SCENE_BACKEND_FAILED;
    }
    wi::graphics::CommandList resolve_command_list = device->BeginCommandList();
    device->QueryResolve(&lod_gpu_timing.heap, 0, lod_gpu_timing.query_count,
        &lod_gpu_timing.readback, 0, resolve_command_list);
    device->SubmitCommandLists();
    device->WaitForGPU();
    const uint64_t* query_results = static_cast<const uint64_t*>(lod_gpu_timing.readback.mapped_data);
    const uint64_t timestamp_frequency = device->GetTimestampFrequency();
    if (query_results == nullptr || timestamp_frequency == 0) {
        lod_gpu_timing = LodGpuTimingState{};
        return ELISA_RENDER_SCENE_BACKEND_FAILED;
    }
    std::vector<uint64_t> gpu_samples;
    gpu_samples.reserve(sample_count);
    for (const auto& range : lod_gpu_timing.ranges) {
        const uint64_t start_tick = query_results[range.first];
        const uint64_t end_tick = query_results[range.second];
        if (end_tick < start_tick) {
            lod_gpu_timing = LodGpuTimingState{};
            return ELISA_RENDER_SCENE_BACKEND_FAILED;
        }
        const long double microseconds = static_cast<long double>(end_tick - start_tick) * 1000000.0L /
            static_cast<long double>(timestamp_frequency);
        gpu_samples.push_back(static_cast<uint64_t>(microseconds));
    }
    std::sort(samples.begin(), samples.end());
    *median_microseconds = samples[sample_count / 2];
    const size_t p95_index = (size_t(sample_count) * 95 + 99) / 100 - 1;
    *p95_microseconds = samples[p95_index];
    std::sort(gpu_samples.begin(), gpu_samples.end());
    const uint64_t gpu_median = gpu_samples[sample_count / 2];
    const uint64_t gpu_p95 = gpu_samples[p95_index];
    std::fprintf(stdout, "LOD completed-frame latency: level=%u samples=%u median=%llu us p95=%llu us\n",
        expected_level, sample_count,
        static_cast<unsigned long long>(*median_microseconds),
        static_cast<unsigned long long>(*p95_microseconds));
    std::fprintf(stdout, "LOD RenderPath GPU timestamps: level=%u samples=%u median=%llu us p95=%llu us\n",
        expected_level, sample_count,
        static_cast<unsigned long long>(gpu_median),
        static_cast<unsigned long long>(gpu_p95));
    lod_gpu_timing = LodGpuTimingState{};
    return ELISA_RENDER_SCENE_OK;
}
