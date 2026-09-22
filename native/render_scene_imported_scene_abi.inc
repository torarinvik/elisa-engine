// Public imported-scene queries keep authored child resources behind the
// RenderScene boundary. Native entity IDs and light generations never cross it.
extern "C" int32_t elisa_render_scene_v1_imported_mesh_count(int64_t handle) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    return int32_t(1 + state.instances[slot].imported_mesh_entities.size());
}

extern "C" int32_t elisa_render_scene_v1_imported_camera_count(int64_t handle) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    return int32_t(state.instances[slot].imported_camera_entities.size());
}

extern "C" int32_t elisa_render_scene_v1_imported_light_count(int64_t handle) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    return int32_t(state.instances[slot].imported_light_handles.size());
}

extern "C" int32_t elisa_render_scene_v1_activate_imported_camera(
    int64_t handle, uint32_t index) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    const InstanceSlot& instance = state.instances[slot];
    if (index >= instance.imported_camera_entities.size()) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    const wi::ecs::Entity entity = instance.imported_camera_entities[index];
    wi::scene::CameraComponent* camera = state.scene->cameras.GetComponent(entity);
    if (camera == nullptr || state.path == nullptr) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    state.camera_entity = entity;
    state.camera = camera;
    state.path->camera = camera;
    sync_camera_settings(state, *camera);
    return resize_unlocked(state, state.width, state.height);
}
