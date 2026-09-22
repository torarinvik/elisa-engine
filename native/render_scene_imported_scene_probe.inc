extern "C" int32_t elisa_render_scene_v1_test_imported_scene(
    int32_t expected_cameras, int32_t expected_lights) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || expected_cameras < 0 || expected_lights < 0) return 0;
    size_t cameras = 0;
    size_t lights = 0;
    wi::ecs::Entity first_camera = wi::ecs::INVALID_ENTITY;
    for (const InstanceSlot& instance : state.instances) {
        if (!instance.live) continue;
        cameras += instance.imported_camera_entities.size();
        lights += instance.imported_light_handles.size();
        if (first_camera == wi::ecs::INVALID_ENTITY && !instance.imported_camera_entities.empty()) {
            first_camera = instance.imported_camera_entities.front();
        }
    }
    const auto* camera = first_camera == wi::ecs::INVALID_ENTITY ? nullptr :
        state.scene->cameras.GetComponent(first_camera);
    return cameras == size_t(expected_cameras) && lights == size_t(expected_lights) &&
        (expected_cameras == 0 || (camera != nullptr && state.camera_entity == first_camera)) ? 1 : 0;
}

extern "C" int32_t elisa_render_scene_v1_test_imported_mesh_count(void) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state)) return -1;
    int32_t count = 0;
    for (const InstanceSlot& instance : state.instances) {
        if (instance.live) count += int32_t(1 + instance.imported_mesh_entities.size());
    }
    return count;
}

extern "C" int32_t elisa_render_scene_v1_test_imported_mesh_visible(
    int64_t handle, uint32_t index) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    wi::ecs::Entity entity = wi::ecs::INVALID_ENTITY;
    if (resolve_imported_mesh(state, handle, index, entity) != ELISA_RENDER_SCENE_OK) return -1;
    const wi::scene::ObjectComponent* object = state.scene->objects.GetComponent(entity);
    return object == nullptr ? -1 : (object->IsRenderable() ? 1 : 0);
}

extern "C" int32_t elisa_render_scene_v1_test_imported_mesh_position(
    int64_t handle, uint32_t index, float px, float py, float pz) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    wi::ecs::Entity entity = wi::ecs::INVALID_ENTITY;
    if (resolve_imported_mesh(state, handle, index, entity) != ELISA_RENDER_SCENE_OK) return -1;
    const wi::scene::TransformComponent* transform = state.scene->transforms.GetComponent(entity);
    if (transform == nullptr) return -1;
    const XMFLOAT3 expected = probe::coordinates::to_wicked(px, py, pz);
    return std::fabs(transform->translation_local.x - expected.x) < 1.0e-5f &&
        std::fabs(transform->translation_local.y - expected.y) < 1.0e-5f &&
        std::fabs(transform->translation_local.z - expected.z) < 1.0e-5f ? 1 : 0;
}

extern "C" int32_t elisa_render_scene_v1_test_imported_light_casts_shadow(
    int64_t handle, uint32_t index) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.lighting == nullptr) return -1;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return -1;
    const InstanceSlot& instance = state.instances[slot];
    if (index >= instance.imported_light_handles.size()) return -1;
    const wi::ecs::Entity entity = state.lighting->entity(instance.imported_light_handles[index]);
    const wi::scene::LightComponent* light = state.scene->lights.GetComponent(entity);
    return light == nullptr ? -1 : (light->IsCastingShadow() ? 1 : 0);
}
