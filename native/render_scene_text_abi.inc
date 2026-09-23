extern "C" int32_t elisa_render_scene_v1_measure_text(const char* text,
    int32_t font_size, float* width, float* height, int32_t* ready) {
    if (!valid_overlay_text_value(text) || font_size < MIN_OVERLAY_FONT_SIZE ||
        font_size > MAX_OVERLAY_FONT_SIZE || width == nullptr || height == nullptr ||
        ready == nullptr) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    try {
        const uint64_t frame = elisa_application_v1_frame_count();
        std::string cache_key = std::to_string(font_size);
        cache_key.push_back('\0');
        cache_key.append(text);
        auto entry = state.overlay_text_measure_cache.find(cache_key);
        if (entry == state.overlay_text_measure_cache.end()) {
            if (state.overlay_text_measure_cache.size() >= MAX_OVERLAY_TEXT_MEASURE_CACHE_ENTRIES) {
                auto oldest = state.overlay_text_measure_cache.begin();
                for (auto candidate = state.overlay_text_measure_cache.begin();
                    candidate != state.overlay_text_measure_cache.end(); ++candidate) {
                    if (candidate->second.last_used_frame < oldest->second.last_used_frame) {
                        oldest = candidate;
                    }
                }
                state.overlay_text_measure_cache.erase(oldest);
            }
            entry = state.overlay_text_measure_cache.emplace(cache_key,
                OverlayTextMeasureCacheEntry{frame, frame, 0.0f, 0.0f, false}).first;
        }
        OverlayTextMeasureCacheEntry& cached = entry->second;
        cached.last_used_frame = frame;

        wi::SpriteFont font;
        font.SetText(text);
        font.params.size = font_size;
        const float measured_width = font.TextWidth();
        const float measured_height = font.TextHeight();
        if (!std::isfinite(measured_width) || !std::isfinite(measured_height) ||
            measured_width < 0.0f || measured_height < 0.0f ||
            measured_width > 1.0e6f || measured_height > 1.0e6f) {
            return ELISA_RENDER_SCENE_BACKEND_FAILED;
        }
        *width = measured_width;
        *height = measured_height;
        if (text[0] == '\0') {
            cached.ready = true;
            cached.ready_width = measured_width;
            cached.ready_height = measured_height;
        } else if (cached.ready) {
            const float tolerance = std::max(0.25f, cached.ready_width * 0.005f);
            if (measured_width + tolerance < cached.ready_width ||
                measured_height + tolerance < cached.ready_height) {
                cached.ready = false;
                cached.requested_frame = frame;
            } else {
                cached.ready_width = measured_width;
                cached.ready_height = measured_height;
            }
        } else if (frame > cached.requested_frame && measured_width > 0.0f) {
            cached.ready = true;
            cached.ready_width = measured_width;
            cached.ready_height = measured_height;
        }
        *ready = cached.ready;
    } catch (...) {
        return ELISA_RENDER_SCENE_BACKEND_FAILED;
    }
    return ELISA_RENDER_SCENE_OK;
}

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

extern "C" int32_t elisa_render_scene_v1_set_text_alignment(
    int64_t handle, int32_t horizontal, int32_t vertical) {
    if (horizontal < wi::font::WIFALIGN_LEFT || horizontal > wi::font::WIFALIGN_RIGHT ||
        (vertical != wi::font::WIFALIGN_TOP &&
         vertical != wi::font::WIFALIGN_CENTER &&
         vertical != wi::font::WIFALIGN_BOTTOM)) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const size_t slot = decode_text_handle(state, handle);
    if (slot == MAX_OVERLAY_TEXTS) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    state.overlay_texts[slot].font.params.h_align =
        static_cast<wi::font::Alignment>(horizontal);
    state.overlay_texts[slot].font.params.v_align =
        static_cast<wi::font::Alignment>(vertical);
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
