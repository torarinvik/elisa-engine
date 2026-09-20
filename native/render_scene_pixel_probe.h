#pragma once

#include <algorithm>

namespace {

constexpr uint32_t ARC_RENDER_PROBE_RADIUS = 8;
constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ull;
constexpr uint64_t FNV_PRIME = 1099511628211ull;

} // namespace

// Test-only GPU readback. render_scene_native_smoke.py enables this symbol;
// ordinary game binaries do not include a blocking probe in the render ABI.
extern "C" int32_t elisa_render_scene_v1_last_frame_center_differs_from_corner(void) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.path == nullptr) return 0;
    for (uint32_t attempt = 0; attempt < PIPELINE_WAIT_ATTEMPTS; ++attempt) {
        if (wi::renderer::IsPipelineCreationActive() == 0) break;
        wi::helper::Sleep(PIPELINE_WAIT_MILLISECONDS);
    }
    if (wi::graphics::GetDevice() != nullptr) wi::graphics::GetDevice()->WaitForGPU();
    const wi::graphics::Texture& frame = state.path->GetRenderResult3D();
    const wi::graphics::TextureDesc& desc = frame.GetDesc();
    const size_t pixel_size = wi::graphics::GetFormatStride(desc.format);
    wi::vector<uint8_t> pixels;
    if (!frame.IsValid() || desc.width == 0 || desc.height == 0 || pixel_size == 0 ||
        !wi::helper::saveTextureToMemoryFile(frame, "RAW", pixels) ||
        pixels.size() < size_t(desc.width) * desc.height * pixel_size) {
        return 0;
    }
    const size_t center = (size_t(desc.height / 2) * desc.width + desc.width / 2) * pixel_size;
    bool differs = false;
    for (size_t index = 0; index < pixel_size; ++index) {
        differs = differs || pixels[index] != pixels[center + index];
    }
    return differs ? 1 : 0;
}

extern "C" uint64_t elisa_render_scene_v1_last_frame_center_patch_hash(void) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.path == nullptr) return 0;
    if (wi::graphics::GetDevice() != nullptr) wi::graphics::GetDevice()->WaitForGPU();
    const wi::graphics::Texture& frame = state.path->GetRenderResult3D();
    const wi::graphics::TextureDesc& desc = frame.GetDesc();
    const size_t pixel_size = wi::graphics::GetFormatStride(desc.format);
    wi::vector<uint8_t> pixels;
    if (!frame.IsValid() || desc.width == 0 || desc.height == 0 || pixel_size == 0 ||
        !wi::helper::saveTextureToMemory(frame, pixels) ||
        pixels.size() < size_t(desc.width) * desc.height * pixel_size) return 0;

    const uint32_t center_x = desc.width / 2;
    const uint32_t center_y = desc.height / 2;
    const uint32_t min_x = center_x > ARC_RENDER_PROBE_RADIUS ? center_x - ARC_RENDER_PROBE_RADIUS : 0;
    const uint32_t min_y = center_y > ARC_RENDER_PROBE_RADIUS ? center_y - ARC_RENDER_PROBE_RADIUS : 0;
    const uint32_t max_x = std::min(desc.width, center_x + ARC_RENDER_PROBE_RADIUS + 1);
    const uint32_t max_y = std::min(desc.height, center_y + ARC_RENDER_PROBE_RADIUS + 1);
    uint64_t hash = FNV_OFFSET_BASIS;
    for (uint32_t y = min_y; y < max_y; ++y) {
        for (uint32_t x = min_x; x < max_x; ++x) {
            const size_t offset = (size_t(y) * desc.width + x) * pixel_size;
            for (size_t channel = 0; channel < pixel_size; ++channel) {
                hash ^= pixels[offset + channel];
                hash *= FNV_PRIME;
            }
        }
    }
    return hash;
}

extern "C" int32_t elisa_render_scene_v1_test_arc_capacity(void) {
    std::array<int64_t, MAX_ELECTRIC_ARCS> handles{};
    size_t created = 0;
    for (; created < MAX_ELECTRIC_ARCS; ++created) {
        const int64_t handle = elisa_render_scene_v1_create_electric_arc(0.02f, 0.0f, 7);
        if (handle <= 0) break;
        handles[created] = handle;
    }

    const int64_t overflow = created == MAX_ELECTRIC_ARCS
        ? elisa_render_scene_v1_create_electric_arc(0.02f, 0.0f, 7)
        : ELISA_RENDER_SCENE_BACKEND_FAILED;
    bool cleanup_ok = true;
    if (overflow > 0) cleanup_ok = elisa_render_scene_v1_destroy_electric_arc(overflow) == ELISA_RENDER_SCENE_OK;
    for (size_t index = 0; index < created; ++index) {
        cleanup_ok = (elisa_render_scene_v1_destroy_electric_arc(handles[index]) == ELISA_RENDER_SCENE_OK) && cleanup_ok;
    }
    return created == MAX_ELECTRIC_ARCS && overflow == ELISA_RENDER_SCENE_CAPACITY && cleanup_ok ? 1 : 0;
}
