extern "C" int64_t elisa_render_scene_v1_create_overlay_panel(float x, float y,
    float width, float height, float red, float green, float blue, float alpha) {
    if (!valid_overlay_panel(x, y, width, height, red, green, blue, alpha)) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (state.path == nullptr) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    const size_t slot = find_free_panel_slot(state);
    if (slot == MAX_OVERLAY_PANELS) return ELISA_RENDER_SCENE_CAPACITY;
    OverlayPanelSlot& entry = state.overlay_panels[slot];
    if (entry.generation >= MAX_PANEL_GENERATION) return ELISA_RENDER_SCENE_GENERATION_EXHAUSTED;
    try {
        if (!apply_overlay_panel(entry.sprite, x, y, width, height, red, green, blue, alpha)) {
            return ELISA_RENDER_SCENE_BACKEND_FAILED;
        }
        state.path->AddSprite(&entry.sprite);
    } catch (...) {
        state.path->RemoveSprite(&entry.sprite);
        entry.sprite.SetHidden(true);
        entry.live = false;
        return ELISA_RENDER_SCENE_BACKEND_FAILED;
    }
    ++entry.generation;
    entry.live = true;
    return int64_t((entry.generation << PANEL_SLOT_BITS) | uint64_t(slot + 1));
}

extern "C" int32_t elisa_render_scene_v1_set_overlay_panel_position(
    int64_t handle, float x, float y) {
    if (!bounded(x, float(MAX_VIEWPORT)) || !bounded(y, float(MAX_VIEWPORT))) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const size_t slot = decode_panel_handle(state, handle);
    if (slot == MAX_OVERLAY_PANELS) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    state.overlay_panels[slot].sprite.params.pos.x = x;
    state.overlay_panels[slot].sprite.params.pos.y = y;
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_set_overlay_panel_size(
    int64_t handle, float width, float height) {
    if (!bounded(width, float(MAX_VIEWPORT)) || !bounded(height, float(MAX_VIEWPORT)) ||
        width <= 0.0f || height <= 0.0f) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const size_t slot = decode_panel_handle(state, handle);
    if (slot == MAX_OVERLAY_PANELS) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    state.overlay_panels[slot].sprite.params.siz = XMFLOAT2(width, height);
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_set_overlay_panel_color(
    int64_t handle, float red, float green, float blue, float alpha) {
    if (!valid_color(red, green, blue, alpha)) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const size_t slot = decode_panel_handle(state, handle);
    if (slot == MAX_OVERLAY_PANELS) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    state.overlay_panels[slot].sprite.params.color = XMFLOAT4(red, green, blue, alpha);
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_set_overlay_panel_visible(
    int64_t handle, int32_t visible) {
    if (visible != 0 && visible != 1) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const size_t slot = decode_panel_handle(state, handle);
    if (slot == MAX_OVERLAY_PANELS) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    state.overlay_panels[slot].sprite.SetHidden(visible == 0);
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_destroy_overlay_panel(int64_t handle) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const size_t slot = decode_panel_handle(state, handle);
    if (slot == MAX_OVERLAY_PANELS) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    OverlayPanelSlot& entry = state.overlay_panels[slot];
    state.path->RemoveSprite(&entry.sprite);
    entry.sprite.SetHidden(true);
    entry.live = false;
    return ELISA_RENDER_SCENE_OK;
}
#include "render_scene_image_abi.inc"
#include "render_scene_debug_abi.inc"
