// Private implementation fragment: cooked mesh loading and instance creation.
extern "C" int64_t elisa_render_scene_v1_create_mesh(
    const char* package_path,
    float px, float py, float pz,
    float qx, float qy, float qz, float qw,
    float sx, float sy, float sz,
    float red, float green, float blue, float alpha) {
    if (!valid_transform(px, py, pz, qx, qy, qz, qw, sx, sy, sz) ||
        !valid_color(red, green, blue, alpha)) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const size_t slot = find_free_slot(state);
    if (slot == MAX_INSTANCES) return ELISA_RENDER_SCENE_CAPACITY;
    InstanceSlot& instance = state.instances[slot];
    if (instance.generation >= MAX_GENERATION) return ELISA_RENDER_SCENE_GENERATION_EXHAUSTED;
    instance.gameplay_epoch = 0;
    instance.gameplay_id = 0;
    instance.render_id = 0;
    instance.mesh_high = 0;
    instance.mesh_low = 0;
    instance.material_high = 0;
    instance.material_low = 0;
    instance.joint_entities.clear();
    instance.skin_joints.clear();
    instance.animation_clips.clear();
    clear_animation_state(instance);
    std::filesystem::path resolved_path;
    elisa::assets::CookedGeometry geometry;
    std::string load_error;
    try {
        if (!elisa::assets::resolve_project_asset_path(package_path, resolved_path)) {
            return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
        }
        if (!elisa::assets::load_cooked_geometry(resolved_path.string(), geometry, load_error)) {
            std::fprintf(stderr, "Elisa cooked mesh load failed: %s\n", load_error.c_str());
            return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Elisa cooked mesh load exception: %s\n", error.what());
        return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
    } catch (...) {
        return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
    }
    const uint64_t generation = instance.generation + 1;
    const std::string name = "elisa_cooked_mesh_" + std::to_string(slot) + "_" + std::to_string(generation);

    wi::ecs::Entity entity = wi::ecs::INVALID_ENTITY;
    std::vector<wi::ecs::Entity> joint_entities;
    try {
        // The cube helper creates the complete Wicked object/mesh/material
        // relationship; replace its small starter geometry with cooked data.
        entity = state.scene->Entity_CreateCube(name);
        if (entity == wi::ecs::INVALID_ENTITY) return ELISA_RENDER_SCENE_BACKEND_FAILED;
        if (!elisa::rendering::configure_cooked_mesh(*state.scene, entity, geometry,
            px, py, pz, qx, qy, qz, qw, sx, sy, sz, red, green, blue, alpha,
            &joint_entities)) {
            state.scene->Entity_Remove(entity);
            return ELISA_RENDER_SCENE_BACKEND_FAILED;
        }
    } catch (...) {
        if (entity != wi::ecs::INVALID_ENTITY) state.scene->Entity_Remove(entity);
        return ELISA_RENDER_SCENE_BACKEND_FAILED;
    }

    instance.entity = entity;
    instance.joint_entities = std::move(joint_entities);
    instance.skin_joints = std::move(geometry.skin_joints);
    instance.animation_clips = std::move(geometry.animation_clips);
    instance.generation = generation;
    instance.live = true;
    return int64_t(encode_handle(slot, generation));
}
