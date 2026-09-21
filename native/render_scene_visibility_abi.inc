extern "C" int32_t elisa_render_scene_v1_set_bloom(int32_t enabled, float threshold) {
    if ((enabled != 0 && enabled != 1) || !finite(threshold) || threshold < 0.0f ||
        threshold > elisa::render_scene_effects::MAX_BLOOM_THRESHOLD) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    elisa::render_scene_effects::apply_bloom(*state.path, enabled != 0, threshold);
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_set_visible(int64_t handle, int32_t visible) {
    if (visible != 0 && visible != 1) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    wi::scene::ObjectComponent* object = state.scene->objects.GetComponent(state.instances[slot].entity);
    if (object == nullptr) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    object->SetRenderable(visible != 0);
    return ELISA_RENDER_SCENE_OK;
}
