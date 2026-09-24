extern "C" int32_t elisa_application_v1_active_path_center_differs_from_corner(void* render_path) {
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized || !on_owner_thread(service) || render_path == nullptr ||
        service.host.wicked().GetActivePath() != static_cast<wi::RenderPath*>(render_path) ||
        wi::graphics::GetDevice() == nullptr) {
        return 0;
    }
    wi::graphics::GetDevice()->WaitForGPU();
    const wi::graphics::Texture frame = wi::graphics::GetDevice()->GetBackBuffer(
        &service.host.wicked().swapChain);
    const wi::graphics::TextureDesc& desc = frame.GetDesc();
    wi::vector<uint8_t> rgba;
    if (!frame.IsValid() || desc.width == 0 || desc.height == 0 ||
        wi::graphics::GetFormatStride(desc.format) != sizeof(uint32_t) ||
        !wi::helper::saveTextureToMemoryFile(frame, "RAW", rgba) ||
        rgba.size() < size_t(desc.width) * desc.height * sizeof(uint32_t)) {
        return 0;
    }
    const size_t center_index = (size_t(desc.height / 2) * desc.width + desc.width / 2) * sizeof(uint32_t);
    const uint32_t corner = uint32_t(rgba[0]) | (uint32_t(rgba[1]) << 8) |
        (uint32_t(rgba[2]) << 16) | (uint32_t(rgba[3]) << 24);
    const uint32_t center = uint32_t(rgba[center_index]) | (uint32_t(rgba[center_index + 1]) << 8) |
        (uint32_t(rgba[center_index + 2]) << 16) | (uint32_t(rgba[center_index + 3]) << 24);
    if (center == corner) {
        std::fprintf(stderr, "render evidence: center/corner pixels both 0x%08x in %ux%u frame\n",
            center, desc.width, desc.height);
        return 0;
    }
    return 1;
}
