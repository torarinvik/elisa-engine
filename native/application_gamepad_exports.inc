extern "C" int32_t elisa_application_v1_rumble_gamepad(
    int32_t device_slot, float low_frequency, float high_frequency,
    int32_t duration_ms) {
    constexpr int32_t MAX_RUMBLE_DURATION_MS = 60000;
    if (device_slot < 1 || device_slot > static_cast<int32_t>(GAMEPAD_CAPACITY) ||
        !std::isfinite(low_frequency) || !std::isfinite(high_frequency) ||
        low_frequency < 0.0f || low_frequency > 1.0f ||
        high_frequency < 0.0f || high_frequency > 1.0f ||
        duration_ms < 1 || duration_ms > MAX_RUMBLE_DURATION_MS) {
        return ELISA_APPLICATION_INVALID_ARGUMENT;
    }

    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;

    SDL_Gamepad* gamepad = service.gamepads[static_cast<size_t>(device_slot - 1)].handle;
    if (gamepad == nullptr || !SDL_GamepadConnected(gamepad)) {
        return ELISA_APPLICATION_RUMBLE_UNAVAILABLE;
    }
    const auto amplitude = [](float normalized) -> Uint16 {
        return static_cast<Uint16>(std::lround(normalized * 65535.0f));
    };
    if (!SDL_RumbleGamepad(gamepad, amplitude(low_frequency), amplitude(high_frequency),
            static_cast<Uint32>(duration_ms))) {
        return ELISA_APPLICATION_UNSUPPORTED;
    }
    return ELISA_APPLICATION_RUMBLE_STARTED;
}

extern "C" int32_t elisa_application_v1_gamepad_button_label(
    int32_t device_slot, int32_t portable_button_code, int32_t* label) {
    if (label == nullptr || device_slot < 1 ||
        device_slot > static_cast<int32_t>(GAMEPAD_CAPACITY)) {
        return ELISA_APPLICATION_INVALID_ARGUMENT;
    }
    const SDL_GamepadButton button =
        probe::gamepad_button_from_code(portable_button_code);
    if (static_cast<int>(button) < 0) return ELISA_APPLICATION_INVALID_ARGUMENT;

    *label = ELISA_APPLICATION_GAMEPAD_LABEL_UNKNOWN;
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;

    SDL_Gamepad* gamepad = service.gamepads[static_cast<size_t>(device_slot - 1)].handle;
    if (gamepad == nullptr || !SDL_GamepadConnected(gamepad)) {
        return ELISA_APPLICATION_GAMEPAD_LABEL_UNAVAILABLE;
    }
    *label = probe::gamepad_button_label_code(SDL_GetGamepadButtonLabel(gamepad, button));
    return *label == ELISA_APPLICATION_GAMEPAD_LABEL_UNKNOWN
        ? ELISA_APPLICATION_GAMEPAD_LABEL_UNAVAILABLE
        : ELISA_APPLICATION_GAMEPAD_LABEL_AVAILABLE;
}

extern "C" int32_t elisa_application_v1_gamepad_connected(int32_t device_slot) {
    if (device_slot < 1 || device_slot > static_cast<int32_t>(GAMEPAD_CAPACITY)) {
        return ELISA_APPLICATION_INVALID_ARGUMENT;
    }
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;

    SDL_Gamepad* gamepad = service.gamepads[static_cast<size_t>(device_slot - 1)].handle;
    return gamepad != nullptr && SDL_GamepadConnected(gamepad) ? 1 : 0;
}
