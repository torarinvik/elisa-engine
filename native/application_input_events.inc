#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
extern "C" int32_t elisa_application_v1_test_push_pointer_event(
    int32_t kind, int32_t button, float x, float y, float delta_x,
    float delta_y, uint32_t buttons, int32_t pressed) {
    if (kind < ELISA_APPLICATION_POINTER_MOTION || kind > ELISA_APPLICATION_POINTER_WHEEL ||
        button < 0 || button > 255 || pressed < 0 || pressed > 1 ||
        !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(delta_x) ||
        !std::isfinite(delta_y)) return ELISA_APPLICATION_INVALID_ARGUMENT;
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    SDL_Event event{};
    const SDL_WindowID window_id = SDL_GetWindowID(service.host.window());
    if (kind == ELISA_APPLICATION_POINTER_MOTION) {
        event.motion.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.windowID = window_id;
        event.motion.state = buttons;
        event.motion.x = x;
        event.motion.y = y;
        event.motion.xrel = delta_x;
        event.motion.yrel = delta_y;
    } else if (kind == ELISA_APPLICATION_POINTER_BUTTON) {
        event.button.type = pressed != 0 ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.windowID = window_id;
        event.button.button = static_cast<Uint8>(button);
        event.button.down = pressed != 0;
        event.button.x = x;
        event.button.y = y;
    } else {
        event.wheel.type = SDL_EVENT_MOUSE_WHEEL;
        event.wheel.windowID = window_id;
        event.wheel.x = delta_x;
        event.wheel.y = delta_y;
        event.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
        event.wheel.mouse_x = x;
        event.wheel.mouse_y = y;
    }
    return SDL_PushEvent(&event) ? ELISA_APPLICATION_OK : ELISA_APPLICATION_FRAME_FAILED;
}
#endif

extern "C" int32_t elisa_application_v1_next_input_event(
    int32_t* kind, int32_t* device, int64_t* code, float* value,
    int32_t* pressed, int32_t* released, int32_t* chord_down) {
    if (kind == nullptr || device == nullptr || code == nullptr || value == nullptr ||
        pressed == nullptr || released == nullptr || chord_down == nullptr) {
        return ELISA_APPLICATION_INVALID_ARGUMENT;
    }
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    if (service.input_event_read < service.input_event_count) {
        const QueuedInputEvent& event = service.input_events[service.input_event_read++];
        *kind = event.kind;
        *device = event.device;
        *code = event.code;
        *value = event.value;
        *pressed = event.pressed;
        *released = event.released;
        *chord_down = event.chord_down;
        return 1;
    }
    if (service.input_overflow && !service.input_overflow_reported) {
        service.input_overflow_reported = true;
        *kind = ELISA_APPLICATION_INPUT_OVERFLOW;
        *device = -1;
        *code = 0;
        *value = 0.0f;
        *pressed = 0;
        *released = 1;
        *chord_down = 0;
        return 1;
    }
    service.input_event_count = 0;
    service.input_event_read = 0;
    service.input_overflow = false;
    service.input_overflow_reported = false;
    return 0;
}

extern "C" int64_t elisa_application_v1_next_input_event_token(void) {
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    int32_t kind = 0;
    int32_t device = 0;
    int64_t code = 0;
    float value = 0.0f;
    bool pressed = false;
    bool released = false;
    if (service.input_event_read < service.input_event_count) {
        const QueuedInputEvent& event = service.input_events[service.input_event_read++];
        kind = event.kind;
        device = event.device;
        code = event.code;
        value = event.value;
        pressed = event.pressed != 0;
        released = event.released != 0;
    } else if (service.input_overflow && !service.input_overflow_reported) {
        service.input_overflow_reported = true;
        kind = ELISA_APPLICATION_INPUT_OVERFLOW;
    } else {
        service.input_event_count = 0;
        service.input_event_read = 0;
        service.input_overflow = false;
        service.input_overflow_reported = false;
        return 0;
    }
    return probe::pack_input_event_token(kind, device, code, value, pressed, released);
}

extern "C" int32_t elisa_application_v1_next_pointer_event(
    int32_t* kind, int32_t* button, float* x, float* y,
    float* delta_x, float* delta_y, uint32_t* buttons, int32_t* pressed) {
    if (kind == nullptr || button == nullptr || x == nullptr || y == nullptr ||
        delta_x == nullptr || delta_y == nullptr || buttons == nullptr ||
        pressed == nullptr) return ELISA_APPLICATION_INVALID_ARGUMENT;
    ApplicationService& service = application_service();
    std::lock_guard<std::mutex> guard(service.mutex);
    if (!service.initialized) return ELISA_APPLICATION_INVALID_STATE;
    if (!on_owner_thread(service)) return ELISA_APPLICATION_WRONG_THREAD;
    if (service.pointer_event_read < service.pointer_event_count) {
        const QueuedPointerEvent& event = service.pointer_events[service.pointer_event_read++];
        *kind = event.kind;
        *button = event.button;
        *x = event.x;
        *y = event.y;
        *delta_x = event.delta_x;
        *delta_y = event.delta_y;
        *buttons = event.buttons;
        *pressed = event.pressed;
        return 1;
    }
    if (service.pointer_overflow && !service.pointer_overflow_reported) {
        service.pointer_overflow_reported = true;
        *kind = ELISA_APPLICATION_POINTER_OVERFLOW;
        *button = 0;
        *x = 0.0f;
        *y = 0.0f;
        *delta_x = 0.0f;
        *delta_y = 0.0f;
        *buttons = 0;
        *pressed = 0;
        return 1;
    }
    service.pointer_event_count = 0;
    service.pointer_event_read = 0;
    service.pointer_overflow = false;
    service.pointer_overflow_reported = false;
    return 0;
}

