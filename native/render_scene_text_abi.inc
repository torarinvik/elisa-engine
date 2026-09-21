extern "C" int64_t elisa_render_scene_v1_create_text(const char* text,
    float x, float y, int32_t font_size, float red, float green, float blue, float alpha) {
    if (!valid_overlay_text(text, x, y, font_size, red, green, blue, alpha)) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const size_t slot = find_free_text_slot(state);
    if (slot == MAX_OVERLAY_TEXTS) return ELISA_RENDER_SCENE_CAPACITY;
    OverlayTextSlot& entry = state.overlay_texts[slot];
    if (entry.generation >= MAX_TEXT_GENERATION) return ELISA_RENDER_SCENE_GENERATION_EXHAUSTED;
    try {
        apply_overlay_text(entry.font, text, x, y, font_size, red, green, blue, alpha);
        state.path->AddFont(&entry.font);
    } catch (...) {
        state.path->RemoveFont(&entry.font);
        entry.font.SetHidden(true);
        entry.live = false;
        return ELISA_RENDER_SCENE_BACKEND_FAILED;
    }
    ++entry.generation;
    entry.live = true;
    return int64_t((entry.generation << TEXT_SLOT_BITS) | uint64_t(slot + TEXT_SLOT_BASE));
}

extern "C" int32_t elisa_render_scene_v1_set_text(int64_t handle, const char* text) {
    if (!valid_overlay_text_value(text)) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const size_t slot = decode_text_handle(state, handle);
    if (slot == MAX_OVERLAY_TEXTS) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    try {
        state.overlay_texts[slot].font.SetText(text);
    } catch (...) {
        return ELISA_RENDER_SCENE_BACKEND_FAILED;
    }
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_set_text_i64(int64_t handle,
    const char* prefix, int64_t value) {
    if (!valid_overlay_text_prefix(prefix)) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const size_t slot = decode_text_handle(state, handle);
    if (slot == MAX_OVERLAY_TEXTS) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    std::array<char, OVERLAY_TEXT_BUFFER_BYTES> formatted{};
    const int written = std::snprintf(formatted.data(), formatted.size(), "%s%lld",
        prefix, static_cast<long long>(value));
    if (written < 0 || size_t(written) > MAX_OVERLAY_TEXT_BYTES) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    try {
        state.overlay_texts[slot].font.SetText(formatted.data());
    } catch (...) {
        return ELISA_RENDER_SCENE_BACKEND_FAILED;
    }
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_set_text_position(
    int64_t handle, float x, float y) {
    if (!bounded(x, float(MAX_VIEWPORT)) || !bounded(y, float(MAX_VIEWPORT))) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const size_t slot = decode_text_handle(state, handle);
    if (slot == MAX_OVERLAY_TEXTS) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    state.overlay_texts[slot].font.params.position.x = x;
    state.overlay_texts[slot].font.params.position.y = y;
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_set_text_size(
    int64_t handle, int32_t font_size) {
    if (font_size < MIN_OVERLAY_FONT_SIZE || font_size > MAX_OVERLAY_FONT_SIZE) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const size_t slot = decode_text_handle(state, handle);
    if (slot == MAX_OVERLAY_TEXTS) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    state.overlay_texts[slot].font.params.size = font_size;
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_set_text_color(
    int64_t handle, float red, float green, float blue, float alpha) {
    if (!valid_color(red, green, blue, alpha)) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const size_t slot = decode_text_handle(state, handle);
    if (slot == MAX_OVERLAY_TEXTS) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    state.overlay_texts[slot].font.params.color =
        wi::Color::fromFloat4(XMFLOAT4(red, green, blue, alpha));
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_set_text_visible(int64_t handle, int32_t visible) {
    if (visible != 0 && visible != 1) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const size_t slot = decode_text_handle(state, handle);
    if (slot == MAX_OVERLAY_TEXTS) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    state.overlay_texts[slot].font.SetHidden(visible == 0);
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_destroy_text(int64_t handle) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const size_t slot = decode_text_handle(state, handle);
    if (slot == MAX_OVERLAY_TEXTS) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    OverlayTextSlot& entry = state.overlay_texts[slot];
    state.path->RemoveFont(&entry.font);
    entry.font.SetHidden(true);
    entry.live = false;
    return ELISA_RENDER_SCENE_OK;
}
