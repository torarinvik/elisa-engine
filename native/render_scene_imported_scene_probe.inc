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
