extern "C" int32_t elisa_application_v1_activate_render_path(void* render_path) {
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    service.host.wicked().ActivatePath(static_cast<wi::RenderPath*>(render_path));
    return ELISA_APPLICATION_OK;
}

extern "C" int32_t elisa_application_v1_register_shutdown_hook(
    void* context, elisa_application_shutdown_fn function) {
    if (context == nullptr || function == nullptr) return ELISA_APPLICATION_INVALID_ARGUMENT;
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    return service.host.add_shutdown_hook(context, function)
        ? ELISA_APPLICATION_OK : ELISA_APPLICATION_INITIALIZATION_FAILED;
}

// Non-deterministic gameplay seed, available before opening a window.
extern "C" uint64_t elisa_application_v1_random_seed(void) {
    try {
        std::random_device source;
        return (static_cast<uint64_t>(source()) << 32) ^ source();
    } catch (...) {
        return static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count());
    }
}
