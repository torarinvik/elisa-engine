bool valid_debug_depth(int32_t depth_tested) {
    return depth_tested == 0 || depth_tested == 1;
}

bool valid_debug_point(float x, float y, float z) {
    return bounded(x) && bounded(y) && bounded(z);
}

bool valid_debug_box(float min_x, float min_y, float min_z,
    float max_x, float max_y, float max_z) {
    return valid_debug_point(min_x, min_y, min_z) && valid_debug_point(max_x, max_y, max_z) &&
        min_x <= max_x && min_y <= max_y && min_z <= max_z;
}

extern "C" int32_t elisa_render_scene_v1_debug_box(
    float min_x, float min_y, float min_z,
    float max_x, float max_y, float max_z,
    float red, float green, float blue, float alpha,
    int32_t depth_tested) {
    if (!valid_debug_box(min_x, min_y, min_z, max_x, max_y, max_z) ||
        !valid_color(red, green, blue, alpha) || !valid_debug_depth(depth_tested)) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    return state.debug_draw.box(XMFLOAT3(min_x, min_y, min_z),
        XMFLOAT3(max_x, max_y, max_z), XMFLOAT4(red, green, blue, alpha),
        depth_tested != 0) ? ELISA_RENDER_SCENE_OK : ELISA_RENDER_SCENE_CAPACITY;
}

extern "C" int32_t elisa_render_scene_v1_debug_line(
    float start_x, float start_y, float start_z,
    float end_x, float end_y, float end_z,
    float red, float green, float blue, float alpha,
    int32_t depth_tested) {
    if (!valid_debug_point(start_x, start_y, start_z) ||
        !valid_debug_point(end_x, end_y, end_z) || !valid_color(red, green, blue, alpha) ||
        !valid_debug_depth(depth_tested)) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    return state.debug_draw.line(XMFLOAT3(start_x, start_y, start_z),
        XMFLOAT3(end_x, end_y, end_z), XMFLOAT4(red, green, blue, alpha),
        depth_tested != 0) ? ELISA_RENDER_SCENE_OK : ELISA_RENDER_SCENE_CAPACITY;
}

extern "C" int32_t elisa_render_scene_v1_debug_text(
    const char* text, float x, float y, float z,
    float red, float green, float blue, float alpha,
    int32_t depth_tested) {
    if (text == nullptr || !valid_debug_point(x, y, z) || !valid_color(red, green, blue, alpha) ||
        !valid_debug_depth(depth_tested)) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    return state.debug_draw.text(XMFLOAT3(x, y, z), text,
        XMFLOAT4(red, green, blue, alpha), depth_tested != 0) ?
        ELISA_RENDER_SCENE_OK : ELISA_RENDER_SCENE_CAPACITY;
}

extern "C" int32_t elisa_render_scene_v1_debug_flush(void) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    return int32_t(state.debug_draw.flush());
}

extern "C" int32_t elisa_render_scene_v1_debug_clear(void) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    state.debug_draw.clear();
    return ELISA_RENDER_SCENE_OK;
}
