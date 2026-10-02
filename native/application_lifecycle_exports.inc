extern "C" int32_t elisa_application_v1_request_exit(void) {
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    service.host.request_close();
    service.pending_events |= ELISA_APPLICATION_EVENT_CLOSE_REQUESTED;
    return ELISA_APPLICATION_OK;
}

extern "C" int32_t elisa_application_v1_shutdown(void) {
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_OK;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    for (OpenGamepad& gamepad : service.gamepads) {
        if (gamepad.handle != nullptr) SDL_CloseGamepad(gamepad.handle);
        gamepad = OpenGamepad{};
    }
    // Detach a service-owned active path before shutdown hooks release it; drop streamed textures.
    service.host.wicked().ActivatePath(nullptr);
    elisa_application_v1_asset_stream_reset();
    elisa_physics_v1_shutdown_from_application();
    elisa_audio_v1_shutdown_from_application();
    flush_application_capture_requests(service);
    service.host.shutdown();
    service.initialized = false;
    service.backend_profile = {};
    service.backend_profile_valid = false;
    service.owner_thread = std::thread::id{};
    service.initialized_at = std::chrono::steady_clock::time_point{};
    service.pending_events = 0;
    service.elapsed_nanos = 0;
    service.input_event_count = 0;
    service.input_event_read = 0;
    service.pointer_event_count = 0;
    service.pointer_event_read = 0;
    service.input_overflow = false;
    service.input_overflow_reported = false;
    service.pointer_overflow = false;
    service.pointer_overflow_reported = false;
    return ELISA_APPLICATION_OK;
}

extern "C" uint64_t elisa_application_v1_frame_count(void) {
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    return service.frame_count;
}

extern "C" uint64_t elisa_application_v1_uptime_nanos(void) {
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return 0;
    const auto elapsed = std::chrono::steady_clock::now() - service.initialized_at;
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
}

extern "C" int32_t elisa_application_v1_validate_owner_thread(void) {
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    return ELISA_APPLICATION_OK;
}
