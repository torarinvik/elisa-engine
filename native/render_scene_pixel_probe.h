#pragma once

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

extern "C" int32_t elisa_render_scene_v1_last_frame_has_overlay_text(void) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.path == nullptr) return 0;
    if (wi::graphics::GetDevice() != nullptr) wi::graphics::GetDevice()->WaitForGPU();
    const wi::graphics::Texture& frame = state.path->GetRenderResult2D();
    const wi::graphics::TextureDesc& desc = frame.GetDesc();
    wi::vector<uint8_t> pixels;
    if (!frame.IsValid() || desc.format != wi::graphics::Format::R8G8B8A8_UNORM ||
        desc.width == 0 || desc.height == 0 ||
        !wi::helper::saveTextureToMemoryFile(frame, "RAW", pixels) ||
        pixels.size() < size_t(desc.width) * desc.height * 4) return 0;
    const uint32_t max_x = std::min(desc.width, 240u);
    const uint32_t max_y = std::min(desc.height, 80u);
    for (uint32_t y = 0; y < max_y; ++y) {
        for (uint32_t x = 0; x < max_x; ++x) {
            const size_t index = (size_t(y) * desc.width + x) * 4;
            if (pixels[index + 3] > 16 &&
                (pixels[index] > 80 || pixels[index + 1] > 80 || pixels[index + 2] > 80)) return 1;
        }
    }
    return 0;
}
