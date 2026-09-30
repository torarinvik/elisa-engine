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

// Live resource totals for leak checks: engine-owned instance slots, live
// lights, and every transform-bearing entity in the Wicked scene.
extern "C" int32_t elisa_render_scene_v1_resource_counts(int32_t* instances,
    int32_t* lights, int32_t* scene_entities) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (instances == nullptr || lights == nullptr || scene_entities == nullptr) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    int32_t live_instances = 0;
    for (const InstanceSlot& slot : state.instances) {
        live_instances += slot.entity != wi::ecs::INVALID_ENTITY ? 1 : 0;
    }
    int32_t live_lights = 0;
    for (const RenderLightSlot& light : state.lights) live_lights += light.live ? 1 : 0;
    *instances = live_instances;
    *lights = live_lights;
    *scene_entities = static_cast<int32_t>(state.scene->transforms.GetCount());
    return ELISA_RENDER_SCENE_OK;
}

// Read back what Wicked holds for one instance: its world-space position
// in Elisa axes (valid after a frame update), and its material's PBR flag and parameters.
extern "C" int32_t elisa_render_scene_v1_instance_probe(int64_t handle, float* position,
    int32_t* lit, float* roughness, float* metallic) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (position == nullptr || lit == nullptr || roughness == nullptr || metallic == nullptr) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    wi::ecs::Entity entity = state.instances[slot].entity;
    const auto* transform = state.scene->transforms.GetComponent(entity);
    const auto* material = state.scene->materials.GetComponent(entity);
    if (transform == nullptr || material == nullptr) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    XMFLOAT3 world = probe::coordinates::from_wicked(transform->GetPosition());
    position[0] = world.x;
    position[1] = world.y;
    position[2] = world.z;
    *lit = material->shaderType == wi::scene::MaterialComponent::SHADERTYPE_PBR ? 1 : 0;
    *roughness = material->roughness;
    *metallic = material->metalness;
    return ELISA_RENDER_SCENE_OK;
}
