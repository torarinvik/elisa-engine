extern "C" int32_t elisa_render_scene_v1_set_pick_identity(
    int64_t handle, int64_t gameplay_epoch, int64_t gameplay_id) {
    if (gameplay_epoch <= 0 || gameplay_id <= 0) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    return set_instance_pick_identity(state, state.instances[slot], gameplay_epoch, gameplay_id) ?
        ELISA_RENDER_SCENE_OK : ELISA_RENDER_SCENE_CAPACITY;
}

extern "C" int32_t elisa_render_scene_v1_pick(
    float origin_x, float origin_y, float origin_z,
    float direction_x, float direction_y, float direction_z,
    uint32_t layer_mask, int64_t* gameplay_epoch, int64_t* gameplay_id,
    float* distance) {
    if (gameplay_epoch == nullptr || gameplay_id == nullptr || distance == nullptr ||
        !bounded(origin_x) || !bounded(origin_y) || !bounded(origin_z) ||
        !bounded(direction_x) || !bounded(direction_y) || !bounded(direction_z) ||
        !finite(direction_x * direction_x + direction_y * direction_y + direction_z * direction_z) ||
        direction_x * direction_x + direction_y * direction_y + direction_z * direction_z <= 1.0e-12f) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (state.picking == nullptr) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    const wi::primitive::Ray ray(
        probe::coordinates::to_wicked(XMFLOAT3(origin_x, origin_y, origin_z)),
        probe::coordinates::to_wicked_direction(XMFLOAT3(direction_x, direction_y, direction_z)),
        0.0f, MAX_SCENE_MAGNITUDE);
    probe::PickGameplayRef gameplay;
    if (!state.picking->pick(ray, layer_mask, gameplay, *distance)) return ELISA_RENDER_SCENE_NO_HIT;
    *gameplay_epoch = gameplay.world_epoch;
    *gameplay_id = gameplay.entity_id;
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_pick_and_select(
    float origin_x, float origin_y, float origin_z,
    float direction_x, float direction_y, float direction_z,
    uint32_t layer_mask, int32_t* has_hit,
    int64_t* gameplay_epoch, int64_t* gameplay_id, float* distance) {
    if (has_hit == nullptr || gameplay_epoch == nullptr || gameplay_id == nullptr || distance == nullptr ||
        !bounded(origin_x) || !bounded(origin_y) || !bounded(origin_z) ||
        !bounded(direction_x) || !bounded(direction_y) || !bounded(direction_z) ||
        !finite(direction_x * direction_x + direction_y * direction_y + direction_z * direction_z) ||
        direction_x * direction_x + direction_y * direction_y + direction_z * direction_z <= 1.0e-12f) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (state.picking == nullptr || state.selection == nullptr) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    *has_hit = 0;
    *gameplay_epoch = 0;
    *gameplay_id = 0;
    *distance = 0.0f;
    const wi::primitive::Ray ray(
        probe::coordinates::to_wicked(XMFLOAT3(origin_x, origin_y, origin_z)),
        probe::coordinates::to_wicked_direction(XMFLOAT3(direction_x, direction_y, direction_z)),
        0.0f, MAX_SCENE_MAGNITUDE);
    probe::PickGameplayRef gameplay;
    wi::ecs::Entity native_entity = wi::ecs::INVALID_ENTITY;
    if (!state.picking->pick(ray, layer_mask, gameplay, native_entity, *distance)) {
        state.selection->clear();
        return ELISA_RENDER_SCENE_OK;
    }
    if (!state.selection->select(native_entity)) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    *has_hit = 1;
    *gameplay_epoch = gameplay.world_epoch;
    *gameplay_id = gameplay.entity_id;
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_select(int64_t handle) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (state.selection == nullptr) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    return state.selection->select(state.instances[slot].entity) ?
        ELISA_RENDER_SCENE_OK : ELISA_RENDER_SCENE_BACKEND_FAILED;
}

extern "C" int32_t elisa_render_scene_v1_clear_selection(void) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (state.selection == nullptr) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    state.selection->clear();
    return ELISA_RENDER_SCENE_OK;
}

#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
extern "C" int32_t elisa_render_scene_v1_test_selection_state(int64_t handle) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    size_t slot = MAX_INSTANCES;
    if (!state.initialized || state.selection == nullptr || !valid_handle(state, handle, slot)) return 0;
    const wi::scene::ObjectComponent* object = state.scene->objects.GetComponent(state.instances[slot].entity);
    if (object == nullptr || !state.selection->selected(state.instances[slot].entity)) return 0;
    const wi::scene::MeshComponent* mesh = state.scene->meshes.GetComponent(object->meshID);
    if (mesh == nullptr || mesh->subsets.empty()) return 0;
    const wi::scene::MaterialComponent* material = state.scene->materials.GetComponent(mesh->subsets.front().materialID);
    return state.path != nullptr && state.path->getOutlineEnabled() && material != nullptr &&
        material->IsOutlineEnabled() ? 1 : 0;
}
#endif
