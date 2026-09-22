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

extern "C" int32_t elisa_render_scene_v1_set_visibility_policy(
    int64_t handle, float draw_distance, float lod_bias, uint32_t layer_mask, int32_t renderable) {
    if (renderable != 0 && renderable != 1) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (state.visibility_lod == nullptr) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    wi::ecs::Entity entity = state.instances[slot].entity;
    if (state.scene->objects.GetComponent(entity) == nullptr) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    const probe::VisibilityDesc descriptor{draw_distance, lod_bias, layer_mask, renderable != 0};
    return state.visibility_lod->update_entity(entity, descriptor)
        ? ELISA_RENDER_SCENE_OK : ELISA_RENDER_SCENE_INVALID_ARGUMENT;
}

extern "C" int32_t elisa_render_scene_v1_set_occlusion_culling(int32_t enabled) {
    if (enabled != 0 && enabled != 1) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (state.visibility_lod == nullptr) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    state.visibility_lod->set_occlusion_culling(enabled != 0);
    return ELISA_RENDER_SCENE_OK;
}

#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
extern "C" int32_t elisa_render_scene_v1_test_visibility_policy(
    int64_t handle, float draw_distance, float lod_bias, uint32_t layer_mask,
    int32_t renderable, int32_t occlusion_culling) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.path == nullptr) return 0;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return 0;
    const wi::scene::ObjectComponent* object = state.scene->objects.GetComponent(state.instances[slot].entity);
    if (object == nullptr) return 0;
    const bool values_match = std::abs(object->draw_distance - draw_distance) < 1.0e-5f &&
        std::abs(object->lod_bias - lod_bias) < 1.0e-5f &&
        object->filterMask == layer_mask && object->IsRenderable() == (renderable != 0);
    const bool occlusion_match = state.path->getOcclusionCullingEnabled() == (occlusion_culling != 0);
    return values_match && occlusion_match ? 1 : 0;
}
#endif
