#pragma once
// R20 per-pass frame report probe (render smoke builds only). Three passes are
// measured inside ElisaRenderPath3D while a capture is active:
//   0 update  - PreUpdate + Update on the CPU. It records no command list, so
//               its GPU time is zero by construction, not by estimate.
//   1 scene   - Render(): CPU time, plus GPU timestamps on command lists opened
//               before and after Wicked's own lists (the R17/LOD technique).
//   2 compose - Compose(cmd): CPU time, plus GPU timestamps on cmd.
// A GPU time is -1 ("unavailable") when the device gives no timestamp heap or
// frequency, when a query was never written, or when Apple TBDR returns an
// end stamp before its begin; it is never replaced by a guess. Draws are
// Wicked's visible objects for the scene pass and composed views for the
// compose pass. Uploads are device bytes newly allocated inside the pass.
// Resident bytes are the device's allocation total, reported on compose only.
// Each pass also opens a Tracy zone and every frame ends in FrameMark when the
// build defines TRACY_ENABLE.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

#if defined(TRACY_ENABLE)
#include <tracy/Tracy.hpp>
#define ELISA_FRAME_REPORT_ZONE(name) ZoneScopedN(name); ++frame_report_probe.tracy_zones
#else
#define ELISA_FRAME_REPORT_ZONE(name) ((void)0)
#endif

namespace {

constexpr uint32_t FRAME_REPORT_PASSES = 3;
constexpr uint32_t FRAME_REPORT_FIELDS = 5;
constexpr uint32_t FRAME_REPORT_MAX_FRAMES = 32;

struct FrameReportPass {
    int64_t cpu_us = 0;
    int64_t gpu_us = -1;
    int64_t draws = 0;
    int64_t uploads = 0;
    int64_t bytes = 0;
    uint32_t gpu_begin = UINT32_MAX;
    uint32_t gpu_end = UINT32_MAX;
};

struct FrameReportProbe {
    wi::graphics::GPUQueryHeap heap;
    wi::graphics::GPUBuffer readback;
    bool capturing = false;
    bool gpu_queries = false;
    uint32_t frame = 0;
    uint32_t frames = 0;
    uint32_t next_query = 0;
    uint32_t query_count = 0;
    int64_t tracy_zones = 0;
    int64_t tracy_frames = 0;
    std::vector<FrameReportPass> passes;
};

FrameReportProbe frame_report_probe;

FrameReportPass* frame_report_slot(uint32_t pass) {
    FrameReportProbe& probe = frame_report_probe;
    if (!probe.capturing || pass >= FRAME_REPORT_PASSES || probe.frame >= probe.frames) return nullptr;
    return &probe.passes[size_t(probe.frame) * FRAME_REPORT_PASSES + pass];
}

uint32_t frame_report_stamp(const wi::graphics::CommandList* given) {
    FrameReportProbe& probe = frame_report_probe;
    wi::graphics::GraphicsDevice* device = wi::graphics::GetDevice();
    if (!probe.gpu_queries || device == nullptr || probe.next_query >= probe.query_count) return UINT32_MAX;
    const uint32_t index = probe.next_query++;
    const wi::graphics::CommandList command = given != nullptr ? *given : device->BeginCommandList();
    device->QueryEnd(&probe.heap, index, command);
    return index;
}

uint64_t frame_report_device_bytes() {
    wi::graphics::GraphicsDevice* device = wi::graphics::GetDevice();
    return device == nullptr ? 0 : device->GetMemoryUsage().usage;
}

// Measures one pass while it is in scope; CPU and upload figures accumulate so
// PreUpdate and Update both count towards pass 0.
class FrameReportScope {
public:
    FrameReportScope(uint32_t pass, bool gpu, const wi::graphics::CommandList* command)
        : slot_(frame_report_slot(pass)), gpu_(gpu), command_(command) {
        if (slot_ == nullptr) return;
        started_ = std::chrono::steady_clock::now();
        bytes_before_ = frame_report_device_bytes();
        if (gpu_) slot_->gpu_begin = frame_report_stamp(command_);
    }
    ~FrameReportScope() {
        if (slot_ == nullptr) return;
        if (gpu_) slot_->gpu_end = frame_report_stamp(command_);
        slot_->cpu_us += std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started_).count();
        const uint64_t after = frame_report_device_bytes();
        if (after > bytes_before_) slot_->uploads += int64_t(after - bytes_before_);
    }
    void draws(int64_t count) { if (slot_ != nullptr) slot_->draws += count; }
    void resident() { if (slot_ != nullptr) slot_->bytes = int64_t(frame_report_device_bytes()); }

private:
    FrameReportPass* slot_ = nullptr;
    bool gpu_ = false;
    const wi::graphics::CommandList* command_ = nullptr;
    std::chrono::steady_clock::time_point started_{};
    uint64_t bytes_before_ = 0;
};

void frame_report_resolve() {
    FrameReportProbe& probe = frame_report_probe;
    wi::graphics::GraphicsDevice* device = wi::graphics::GetDevice();
    const uint64_t frequency = device == nullptr ? 0 : device->GetTimestampFrequency();
    if (!probe.gpu_queries || frequency == 0) return;
    wi::graphics::CommandList command = device->BeginCommandList();
    device->QueryResolve(&probe.heap, 0, probe.query_count, &probe.readback, 0, command);
    device->SubmitCommandLists();
    device->WaitForGPU();
    const uint64_t* stamps = static_cast<const uint64_t*>(probe.readback.mapped_data);
    if (stamps == nullptr) return;
    for (FrameReportPass& pass : probe.passes) {
        if (pass.gpu_begin == UINT32_MAX || pass.gpu_end == UINT32_MAX) continue;
        const uint64_t begin = stamps[pass.gpu_begin];
        const uint64_t end = stamps[pass.gpu_end];
        if (begin == 0 || end < begin) continue;
        const long double micros = static_cast<long double>(end - begin) * 1000000.0L /
            static_cast<long double>(frequency);
        if (micros > 1000000.0L) continue;  // TBDR garbage stamp: label unavailable
        pass.gpu_us = int64_t(micros);
    }
}

void frame_report_write_json(const char* path) {
    FrameReportProbe& probe = frame_report_probe;
    FILE* file = std::fopen(path, "w");
    if (file == nullptr) return;
    static const char* const NAMES[FRAME_REPORT_PASSES] = {"update", "scene", "compose"};
    wi::graphics::GraphicsDevice* device = wi::graphics::GetDevice();
    std::fprintf(file, "{\"backend\": \"%s\", \"timestamps\": %s, \"tracy\": %s, \"frames\": [\n",
        device == nullptr ? "none" : device->GetTag(), probe.gpu_queries ? "true" : "false",
#if defined(TRACY_ENABLE)
        "true");
#else
        "false");
#endif
    for (uint32_t frame = 0; frame < probe.frames; ++frame) {
        std::fprintf(file, "  [");
        for (uint32_t pass = 0; pass < FRAME_REPORT_PASSES; ++pass) {
            const FrameReportPass& value = probe.passes[size_t(frame) * FRAME_REPORT_PASSES + pass];
            std::fprintf(file, "%s{\"pass\": \"%s\", \"cpu_us\": %lld, \"gpu_us\": ", pass == 0 ? "" : ", ",
                NAMES[pass], (long long)value.cpu_us);
            if (value.gpu_us < 0) std::fprintf(file, "\"unavailable\"");
            else std::fprintf(file, "%lld", (long long)value.gpu_us);
            std::fprintf(file, ", \"draws\": %lld, \"upload_bytes\": %lld, \"resource_bytes\": %lld}",
                (long long)value.draws, (long long)value.uploads, (long long)value.bytes);
        }
        std::fprintf(file, "]%s\n", frame + 1 < probe.frames ? "," : "");
    }
    std::fprintf(file, "]}\n");
    std::fclose(file);
}

} // namespace

// Pumps `frames` frames with the probe active; frame 0 is the caller's cold
// frame. `report_variable` names an environment variable holding a JSON path.
extern "C" int32_t elisa_render_scene_v1_test_frame_report_capture(uint32_t frames, const char* report_variable) {
    FrameReportProbe& probe = frame_report_probe;
    wi::graphics::GraphicsDevice* device = wi::graphics::GetDevice();
    if (frames == 0 || frames > FRAME_REPORT_MAX_FRAMES || device == nullptr || probe.capturing) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    probe = FrameReportProbe{};
    probe.frames = frames;
    probe.passes.assign(size_t(frames) * FRAME_REPORT_PASSES, FrameReportPass{});
    for (FrameReportPass& pass : probe.passes) pass.gpu_us = -1;
    for (uint32_t frame = 0; frame < frames; ++frame) probe.passes[size_t(frame) * FRAME_REPORT_PASSES].gpu_us = 0;
    probe.query_count = frames * 4;
    wi::graphics::GPUQueryHeapDesc heap_desc;
    heap_desc.type = wi::graphics::GpuQueryType::TIMESTAMP;
    heap_desc.query_count = probe.query_count;
    wi::graphics::GPUBufferDesc buffer_desc;
    buffer_desc.usage = wi::graphics::Usage::READBACK;
    buffer_desc.size = uint64_t(probe.query_count) * sizeof(uint64_t);
    probe.gpu_queries = device->GetTimestampFrequency() != 0 &&
        device->CreateQueryHeap(&heap_desc, &probe.heap) &&
        device->CreateBuffer(&buffer_desc, nullptr, &probe.readback);
    if (probe.gpu_queries) {
        wi::graphics::CommandList reset = device->BeginCommandList();
        device->QueryReset(&probe.heap, 0, probe.query_count, reset);
        device->SubmitCommandLists();
        device->WaitForGPU();
    }
    probe.capturing = true;
    for (uint32_t frame = 0; frame < frames; ++frame) {
        probe.frame = frame;
        if (elisa_application_v1_pump() != ELISA_APPLICATION_RUNNING) {
            probe = FrameReportProbe{};
            return ELISA_RENDER_SCENE_BACKEND_FAILED;
        }
        device->WaitForGPU();
#if defined(TRACY_ENABLE)
        const FrameReportPass* row = &probe.passes[size_t(frame) * FRAME_REPORT_PASSES];
        TracyPlot("elisa_scene_cpu_us", row[1].cpu_us);
        TracyPlot("elisa_draws", row[1].draws);
        FrameMark;
        ++probe.tracy_frames;
#endif
    }
    probe.capturing = false;
    probe.frame = frames;
    frame_report_resolve();
    const char* path = report_variable == nullptr ? nullptr : std::getenv(report_variable);
    if (path != nullptr && path[0] != '\0') frame_report_write_json(path);
    probe.heap = {};
    probe.readback = {};
    return ELISA_RENDER_SCENE_OK;
}

// One value of the last capture: field 0 cpu_us, 1 gpu_us (-1 unavailable),
// 2 draws, 3 upload bytes, 4 resident bytes. -2 for an out-of-range request.
extern "C" int64_t elisa_render_scene_v1_test_frame_report_value(uint32_t frame, uint32_t pass, uint32_t field) {
    const FrameReportProbe& probe = frame_report_probe;
    if (probe.capturing || frame >= probe.frames || pass >= FRAME_REPORT_PASSES || field >= FRAME_REPORT_FIELDS) return -2;
    const FrameReportPass& value = probe.passes[size_t(frame) * FRAME_REPORT_PASSES + pass];
    const int64_t fields[FRAME_REPORT_FIELDS] = {value.cpu_us, value.gpu_us, value.draws, value.uploads, value.bytes};
    return fields[field];
}

// Tracy evidence for the last capture: frame marks emitted, or -1 when the
// build has no Tracy client.
extern "C" int64_t elisa_render_scene_v1_test_frame_report_tracy_frames(void) {
#if defined(TRACY_ENABLE)
    return frame_report_probe.tracy_frames;
#else
    return -1;
#endif
}
