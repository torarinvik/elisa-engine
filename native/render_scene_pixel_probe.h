#pragma once

// Test-only readback probe, included by render_scene_abi.cpp.
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
