extern "C" int64_t elisa_render_scene_v1_create_overlay_image(const char* asset_path,
    float x, float y, float width, float height) {
    if (asset_path == nullptr || asset_path[0] == '\0' ||
        !valid_overlay_image_rect(x, y, width, height)) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (state.path == nullptr) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    const size_t slot = find_free_image_slot(state);
    if (slot == MAX_OVERLAY_IMAGES) return ELISA_RENDER_SCENE_CAPACITY;
    OverlayImageSlot& entry = state.overlay_images[slot];
    if (entry.generation >= MAX_IMAGE_GENERATION) return ELISA_RENDER_SCENE_GENERATION_EXHAUSTED;
    try {
        if (!apply_overlay_image(entry, asset_path, x, y, width, height)) {
            release_overlay_image(state, entry);
            return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
        }
        state.path->AddSprite(&entry.sprite);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Elisa overlay image load failed: %s\n", error.what());
        release_overlay_image(state, entry);
        return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
    } catch (...) {
        release_overlay_image(state, entry);
        return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
    }
    ++entry.generation;
    entry.live = true;
    return int64_t((entry.generation << IMAGE_SLOT_BITS) | uint64_t(slot + 1));
}

extern "C" int32_t elisa_render_scene_v1_set_overlay_image_position(
    int64_t handle, float x, float y) {
    if (!bounded(x, float(MAX_VIEWPORT)) || !bounded(y, float(MAX_VIEWPORT))) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const size_t slot = decode_image_handle(state, handle);
    if (slot == MAX_OVERLAY_IMAGES) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    state.overlay_images[slot].sprite.params.pos.x = x;
    state.overlay_images[slot].sprite.params.pos.y = y;
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_set_overlay_image_size(
    int64_t handle, float width, float height) {
    if (!bounded(width, float(MAX_VIEWPORT)) || !bounded(height, float(MAX_VIEWPORT)) ||
        width <= 0.0f || height <= 0.0f) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const size_t slot = decode_image_handle(state, handle);
    if (slot == MAX_OVERLAY_IMAGES) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    state.overlay_images[slot].sprite.params.siz = XMFLOAT2(width, height);
    return ELISA_RENDER_SCENE_OK;
}

// UVs are normalized over the whole atlas with v growing downward.
extern "C" int32_t elisa_render_scene_v1_set_overlay_image_uv(
    int64_t handle, float u0, float v0, float u1, float v1) {
    if (!valid_overlay_image_uv(u0, v0, u1, v1)) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const size_t slot = decode_image_handle(state, handle);
    if (slot == MAX_OVERLAY_IMAGES) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    apply_overlay_image_uv(state.overlay_images[slot], u0, v0, u1, v1);
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_set_overlay_image_color(
    int64_t handle, float red, float green, float blue, float alpha) {
    if (!valid_color(red, green, blue, alpha)) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const size_t slot = decode_image_handle(state, handle);
    if (slot == MAX_OVERLAY_IMAGES) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    state.overlay_images[slot].sprite.params.color = XMFLOAT4(red, green, blue, alpha);
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_set_overlay_image_visible(
    int64_t handle, int32_t visible) {
    if (visible != 0 && visible != 1) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const size_t slot = decode_image_handle(state, handle);
    if (slot == MAX_OVERLAY_IMAGES) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    state.overlay_images[slot].sprite.SetHidden(visible == 0);
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_destroy_overlay_image(int64_t handle) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const size_t slot = decode_image_handle(state, handle);
    if (slot == MAX_OVERLAY_IMAGES) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    release_overlay_image(state, state.overlay_images[slot]);
    return ELISA_RENDER_SCENE_OK;
}
