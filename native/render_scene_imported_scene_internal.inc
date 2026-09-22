void release_imported_scene(RenderSceneService& state, InstanceSlot& instance) {
    if (state.scene == nullptr) {
        instance.imported_camera_entities.clear();
        instance.imported_light_handles.clear();
        return;
    }
    const bool active_imported = std::find(instance.imported_camera_entities.begin(),
        instance.imported_camera_entities.end(), state.camera_entity) != instance.imported_camera_entities.end();
    for (probe::NativeLightHandle light : instance.imported_light_handles) {
        if (state.lighting != nullptr) state.lighting->destroy_light(light);
    }
    for (wi::ecs::Entity camera : instance.imported_camera_entities) state.scene->Entity_Remove(camera);
    instance.imported_camera_entities.clear();
    instance.imported_light_handles.clear();
    if (active_imported && state.primary_camera_entity != wi::ecs::INVALID_ENTITY) {
        state.camera_entity = state.primary_camera_entity;
        state.camera = state.scene->cameras.GetComponent(state.primary_camera_entity);
        if (state.path != nullptr) state.path->camera = state.camera;
        if (state.camera != nullptr) {
            state.perspective_camera = !state.camera->IsOrtho();
            state.perspective_fov = state.camera->fov;
            state.camera_near_clip = state.camera->zNearP;
            state.camera_far_clip = state.camera->zFarP;
            state.vertical_size = state.camera->ortho_vertical_size;
            resize_unlocked(state, state.width, state.height);
        }
    }
}
