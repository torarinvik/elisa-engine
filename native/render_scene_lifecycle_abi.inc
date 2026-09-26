extern "C" int32_t elisa_render_scene_v1_destroy(int64_t handle) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    if (state.selection != nullptr && state.selection->selected(state.instances[slot].entity)) {
        state.selection->clear();
    }
    clear_instance_pick_bindings(state, state.instances[slot]);
    release_animation_submission(state, state.instances[slot]);
    release_imported_scene(state, state.instances[slot]);
    for (wi::ecs::Entity placement : state.instances[slot].snapshot_placement_entities) {
        state.scene->Entity_Remove(placement);
    }
    remove_instance_entity(state, slot);
    for (wi::ecs::Entity joint : state.instances[slot].joint_entities) state.scene->Entity_Remove(joint);
    if (state.instances[slot].shared_mesh_slot < MAX_SNAPSHOT_SHARED_MESHES) {
        release_snapshot_shared_mesh(state, state.instances[slot].shared_mesh_slot);
    }
    for (size_t shared_slot : state.instances[slot].snapshot_additional_shared_mesh_slots) {
        release_snapshot_shared_mesh(state, shared_slot);
    }
    clear_snapshot_instance(state, state.instances[slot]);
    return ELISA_RENDER_SCENE_OK;
}
extern "C" int32_t elisa_render_scene_v1_shutdown(void) {
    RenderSceneService& state = service();
    {
        std::lock_guard<std::mutex> guard(state.mutex);
        if (!state.initialized) return ELISA_RENDER_SCENE_OK;
        if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    }
    const int32_t app_result = elisa_application_v1_activate_render_path(nullptr);
    if (app_result == ELISA_APPLICATION_WRONG_THREAD) return ELISA_RENDER_SCENE_WRONG_THREAD;
    std::lock_guard<std::mutex> guard(state.mutex);
    reset_unlocked(state);
    return ELISA_RENDER_SCENE_OK;
}
extern "C" uint64_t elisa_render_scene_v1_instance_count(void) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    uint64_t count = 0;
    for (const InstanceSlot& instance : state.instances) count += instance.live ? 1 : 0;
    return count;
}
extern "C" int32_t elisa_render_scene_v1_is_initialized(void) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    return state.initialized ? 1 : 0;
}
