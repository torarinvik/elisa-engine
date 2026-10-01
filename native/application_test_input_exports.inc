// Test-only input injection, included once by application_service_exports.inc.
// Pushes a real SDL keyboard event, so it travels the same pump, key-code
// mapping and action-binding path as a key the player presses. Only the keys
// a pilot needs are accepted: 1 Right arrow, 2 Left arrow, 3 Escape.
extern "C" int32_t elisa_application_v1_test_push_key(int32_t key, int32_t down) {
    SDL_Keycode keycode = 0;
    if (key == 1) keycode = SDLK_RIGHT;
    else if (key == 2) keycode = SDLK_LEFT;
    else if (key == 3) keycode = SDLK_ESCAPE;
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
