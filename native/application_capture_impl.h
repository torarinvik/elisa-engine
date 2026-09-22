#pragma once

static bool valid_environment_name(const char* name) {
    if (name == nullptr || name[0] == '\0') return false;
    for (size_t index = 0; index < 128; ++index) {
        const char c = name[index];
        if (c == '\0') return index > 0;
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
        if (!ok) return false;
    }
    return false;
}

extern "C" const char* elisa_application_v1_environment_value(const char* name) {
    if (!valid_environment_name(name)) return "";
    const char* value = std::getenv(name);
    return value == nullptr ? "" : value;
}

extern "C" int64_t elisa_application_v1_environment_integer(const char* name, int64_t fallback) {
    const char* value = elisa_application_v1_environment_value(name);
    if (value[0] == '\0') return fallback;
    char* end = nullptr;
    errno = 0;
    const long long parsed = std::strtoll(value, &end, 10);
    if (end == value || *end != '\0' || errno != 0) return fallback;
    return static_cast<int64_t>(parsed);
}

extern "C" int32_t elisa_application_v1_save_screenshot(const char* path) {
    if (path == nullptr || path[0] == '\0' || std::strlen(path) > 4096) return ELISA_APPLICATION_INVALID_ARGUMENT;
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    if (service.frame_count == 0) return ELISA_APPLICATION_INVALID_STATE;
    wi::graphics::GraphicsDevice* device = wi::graphics::GetDevice();
    if (device == nullptr) return ELISA_APPLICATION_FRAME_FAILED;
    device->WaitForGPU();
    const wi::graphics::Texture presented = device->GetBackBuffer(&service.host.wicked().swapChain);
    if (!presented.IsValid()) return ELISA_APPLICATION_FRAME_FAILED;
    return probe::save_rgba_png(presented, std::string(path)) ? ELISA_APPLICATION_OK : ELISA_APPLICATION_FRAME_FAILED;
}
