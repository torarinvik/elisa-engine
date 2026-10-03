#pragma once

#if defined(ELISA_RENDER_SCENE_TEST_PROBE) || defined(ELISA_APPLICATION_TEST_PROBE)
extern "C" int32_t elisa_application_v1_test_set_minimized(int32_t minimized) {
    if (minimized != 0 && minimized != 1) return ELISA_APPLICATION_INVALID_ARGUMENT;
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    SDL_Window* window = service.host.window();
    if (window == nullptr) return ELISA_APPLICATION_INVALID_STATE;
    const bool requested = minimized != 0
        ? SDL_MinimizeWindow(window)
        : SDL_RestoreWindow(window);
    if (!requested || !SDL_SyncWindow(window)) return ELISA_APPLICATION_FRAME_FAILED;
    SDL_PumpEvents();
    const bool window_is_minimized = (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) != 0;
    if (window_is_minimized != (minimized != 0)) return ELISA_APPLICATION_FRAME_FAILED;
    return ELISA_APPLICATION_OK;
}

extern "C" int32_t elisa_application_v1_test_set_window_size(int32_t width, int32_t height) {
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384)
        return ELISA_APPLICATION_INVALID_ARGUMENT;
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    SDL_Window* window = service.host.window();
    if (window == nullptr || !SDL_SetWindowSize(window, width, height) || !SDL_SyncWindow(window))
        return ELISA_APPLICATION_FRAME_FAILED;
    return ELISA_APPLICATION_OK;
}

// One stress-trace line on stderr, retained in the smoke's log artifact.
extern "C" void elisa_application_v1_test_trace_stress(int32_t iteration, int64_t instances,
    int64_t navmeshes, int64_t voices, int64_t streams, int64_t heap_bytes) {
    std::fprintf(stderr, "stress iteration=%d instances=%lld navmeshes=%lld voices=%lld streams=%lld heap=%lld\n",
        iteration, static_cast<long long>(instances), static_cast<long long>(navmeshes),
        static_cast<long long>(voices), static_cast<long long>(streams), static_cast<long long>(heap_bytes));
    std::fflush(stderr);
}
#endif
