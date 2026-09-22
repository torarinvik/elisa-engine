// Public imported-scene queries keep authored child resources behind the
// RenderScene boundary. Native entity IDs and light generations never cross it.
static int32_t resolve_imported_mesh(RenderSceneService& state, int64_t handle,
    uint32_t index, wi::ecs::Entity& entity) {
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    const InstanceSlot& instance = state.instances[slot];
    if (index == 0) {
        entity = instance.entity;
    } else {
        const size_t child = size_t(index - 1);
        if (child >= instance.imported_mesh_entities.size()) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
        entity = instance.imported_mesh_entities[child];
    }
    if (entity == wi::ecs::INVALID_ENTITY) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    return ELISA_RENDER_SCENE_OK;
}

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

extern "C" int32_t elisa_render_scene_v1_set_imported_mesh_visible(
    int64_t handle, uint32_t index, int32_t visible) {
    if (visible != 0 && visible != 1) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    wi::ecs::Entity entity = wi::ecs::INVALID_ENTITY;
    const int32_t status = resolve_imported_mesh(state, handle, index, entity);
    if (status != ELISA_RENDER_SCENE_OK) return status;
    wi::scene::ObjectComponent* object = state.scene->objects.GetComponent(entity);
    if (object == nullptr) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    object->SetRenderable(visible != 0);
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_update_imported_mesh_transform(
    int64_t handle, uint32_t index,
    float px, float py, float pz,
    float qx, float qy, float qz, float qw,
    float sx, float sy, float sz) {
    if (!valid_transform(px, py, pz, qx, qy, qz, qw, sx, sy, sz)) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    wi::ecs::Entity entity = wi::ecs::INVALID_ENTITY;
    const int32_t status = resolve_imported_mesh(state, handle, index, entity);
    if (status != ELISA_RENDER_SCENE_OK) return status;
    wi::scene::TransformComponent* transform = state.scene->transforms.GetComponent(entity);
    if (transform == nullptr) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    apply_transform(*transform, px, py, pz, qx, qy, qz, qw, sx, sy, sz);
    return ELISA_RENDER_SCENE_OK;
}
