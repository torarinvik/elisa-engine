// Test-only input injection, included once by application_service_exports.inc.
// Pushes a real SDL keyboard event, so it travels the same pump, key-code
// mapping and action-binding path as a key the player presses. Only the keys
// a pilot needs are accepted: 1 Right arrow, 2 Left arrow, 3 Escape,
// 4 Space, 5 C, 6 W, 7 P, 8 R and 9 S.
extern "C" int32_t elisa_application_v1_test_push_key(int32_t key, int32_t down) {
    SDL_Keycode keycode = 0;
    if (key == 1) keycode = SDLK_RIGHT;
    else if (key == 2) keycode = SDLK_LEFT;
    else if (key == 3) keycode = SDLK_ESCAPE;
    else if (key == 4) keycode = SDLK_SPACE;
    else if (key == 5) keycode = SDLK_C;
    else if (key == 6) keycode = SDLK_W;
    else if (key == 7) keycode = SDLK_P;
    else if (key == 8) keycode = SDLK_R;
    else if (key == 9) keycode = SDLK_S;
    else return ELISA_APPLICATION_INVALID_ARGUMENT;
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    SDL_Event event{};
    const SDL_EventType type = down != 0 ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    event.type = type;
    event.key.type = type;
    event.key.down = down != 0;
    event.key.key = keycode;
    event.key.scancode = SDL_GetScancodeFromKey(keycode, nullptr);
    return SDL_PushEvent(&event) ? ELISA_APPLICATION_OK : ELISA_APPLICATION_INITIALIZATION_FAILED;
}

// Synthetic window event for the live course pilot; it exercises SDL event
// translation and gameplay handling, without claiming an OS focus switch.
extern "C" int32_t elisa_application_v1_test_push_focus_lost() {
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    SDL_Window* window = service.host.window();
    if (window == nullptr) return ELISA_APPLICATION_INVALID_STATE;
    SDL_Event event{};
    event.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    event.window.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    event.window.windowID = SDL_GetWindowID(window);
    return SDL_PushEvent(&event) ? ELISA_APPLICATION_OK : ELISA_APPLICATION_INITIALIZATION_FAILED;
}

extern "C" int32_t elisa_application_v1_test_push_focus_gained() {
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    SDL_Window* window = service.host.window();
    if (window == nullptr) return ELISA_APPLICATION_INVALID_STATE;
    SDL_Event event{};
    event.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
    event.window.type = SDL_EVENT_WINDOW_FOCUS_GAINED;
    event.window.windowID = SDL_GetWindowID(window);
    return SDL_PushEvent(&event) ? ELISA_APPLICATION_OK : ELISA_APPLICATION_INITIALIZATION_FAILED;
}

extern "C" void elisa_application_v1_test_trace_cell_memory(int64_t warm_gpu,
    int64_t warm_cpu, int64_t gpu, int64_t cpu) {
    std::fprintf(stderr, "course cell memory warm_gpu=%lld warm_cpu=%lld gpu=%lld cpu=%lld\n",
        static_cast<long long>(warm_gpu), static_cast<long long>(warm_cpu),
        static_cast<long long>(gpu), static_cast<long long>(cpu));
    std::fflush(stderr);
}
