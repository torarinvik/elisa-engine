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
    instance.shared_mesh_slot = NO_SHARED_MESH;
    instance.shared_source = wi::ecs::INVALID_ENTITY;
    instance.imported_mesh_entities.clear();
    instance.imported_camera_entities.clear(); instance.imported_light_handles.clear();
    instance.joint_entities.clear();
    instance.skin_joints.clear();
    instance.animation_clips.clear();
    instance.animation_submission = {};
    clear_animation_state(instance);
    std::filesystem::path resolved_path;
    elisa::assets::CookedGeometry geometry;
    std::string load_error;
    try {
        if (!elisa::assets::resolve_project_asset_path(package_path, resolved_path)) {
            return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
        }
        if (!elisa::assets::verify_bundle_dependencies(resolved_path, load_error) ||
            !elisa::assets::load_cooked_geometry_asset(resolved_path.string(), geometry, load_error)) {
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
    std::vector<wi::ecs::Entity> placement_entities;
    try {
        // The cube helper creates the complete Wicked object/mesh/material
        // relationship; replace its small starter geometry with cooked data.
        entity = state.scene->Entity_CreateCube(name);
        if (entity == wi::ecs::INVALID_ENTITY) return ELISA_RENDER_SCENE_BACKEND_FAILED;
        const bool has_placement_metadata = !geometry.mesh_placements.empty() &&
            geometry.skin_joints.empty() && geometry.skin_cluster_joints.empty() &&
            geometry.morph_targets.empty();
        const bool configured = has_placement_metadata ?
            elisa::rendering::configure_cooked_mesh_placement(*state.scene, entity, geometry, 0,
                px, py, pz, qx, qy, qz, qw, sx, sy, sz, red, green, blue, alpha) :
            elisa::rendering::configure_cooked_mesh(*state.scene, entity, geometry,
                px, py, pz, qx, qy, qz, qw, sx, sy, sz, red, green, blue, alpha,
                &joint_entities);
        if (!configured) {
            state.scene->Entity_Remove(entity);
            return ELISA_RENDER_SCENE_BACKEND_FAILED;
        }
        if (has_placement_metadata) {
            placement_entities.reserve(geometry.mesh_placements.size() - 1);
            for (size_t placement = 1; placement < geometry.mesh_placements.size(); ++placement) {
                const std::string child_name = name + "_placement_" + std::to_string(placement);
                const wi::ecs::Entity child = state.scene->Entity_CreateCube(child_name);
                if (child == wi::ecs::INVALID_ENTITY ||
                    !elisa::rendering::configure_cooked_mesh_placement(*state.scene, child, geometry, placement,
                        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                        1.0f, 1.0f, 1.0f, red, green, blue, alpha)) {
                    if (child != wi::ecs::INVALID_ENTITY) state.scene->Entity_Remove(child);
                    throw 1;
                }
                state.scene->Component_Attach(child, entity, true);
                const auto* parent_transform = state.scene->transforms.GetComponent(entity);
                auto* child_transform = state.scene->transforms.GetComponent(child);
                if (parent_transform == nullptr || child_transform == nullptr) throw 1;
                child_transform->UpdateTransform_Parented(*parent_transform);
                placement_entities.push_back(child);
            }
            instance.imported_mesh_entities = std::move(placement_entities);
        }
        elisa::rendering::CookedSceneResources imported;
        if (state.lighting == nullptr || !elisa::rendering::install_cooked_scene(*state.scene,
            *state.lighting, geometry, state.width, state.height, entity, imported)) {
            for (wi::ecs::Entity joint : joint_entities) state.scene->Entity_Remove(joint);
            state.scene->Entity_Remove(entity);
            return ELISA_RENDER_SCENE_BACKEND_FAILED;
        }
        instance.imported_camera_entities = std::move(imported.cameras);
        instance.imported_light_handles = std::move(imported.lights);
        if (!instance.imported_camera_entities.empty() && state.camera_entity == state.primary_camera_entity) {
            state.camera_entity = instance.imported_camera_entities.front();
            state.camera = state.scene->cameras.GetComponent(state.camera_entity);
            if (state.camera == nullptr || state.path == nullptr) throw 1;
            state.path->camera = state.camera;
            state.perspective_camera = !state.camera->IsOrtho();
            state.perspective_fov = state.camera->fov;
            state.camera_near_clip = state.camera->zNearP;
            state.camera_far_clip = state.camera->zFarP;
            state.vertical_size = state.camera->ortho_vertical_size;
        }
    } catch (...) {
        release_imported_scene(state, instance);
        for (wi::ecs::Entity mesh : placement_entities) state.scene->Entity_Remove(mesh);
        for (wi::ecs::Entity joint : joint_entities) state.scene->Entity_Remove(joint);
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

// Shared-mesh instances. Wicked draws every ObjectComponent naming one mesh
// entity from that mesh's buffers, so a clone costs an object and a transform
// rather than another cooked load and GPU mesh. Only static sources qualify:
// skins, clips and morph targets animate per entity.
bool shared_source_static(const RenderSceneService& state, const InstanceSlot& source) {
    if (source.shared_source != wi::ecs::INVALID_ENTITY || !source.skin_joints.empty() ||
        !source.animation_clips.empty() || !source.joint_entities.empty() ||
        !source.imported_mesh_entities.empty()) return false;
    const wi::scene::MeshComponent* mesh = state.scene->meshes.GetComponent(source.entity);
    return mesh != nullptr && mesh->morph_targets.empty() &&
        state.scene->objects.GetComponent(source.entity) != nullptr;
}

bool other_instance_uses_mesh(const RenderSceneService& state, size_t slot, wi::ecs::Entity mesh_entity) {
    for (size_t index = 0; index < MAX_INSTANCES; ++index) {
        const InstanceSlot& other = state.instances[index];
        if (index == slot || !other.live) continue;
        if (other.entity == mesh_entity || other.shared_source == mesh_entity) return true;
    }
    return false;
}

// A source whose clones still draw keeps its mesh and material components
// until the last clone goes; that clone's removal then frees the retired
// source entity.
void remove_instance_entity(RenderSceneService& state, size_t slot) {
    const wi::ecs::Entity entity = state.instances[slot].entity;
    const wi::ecs::Entity source = state.instances[slot].shared_source;
    if (source == wi::ecs::INVALID_ENTITY) {
        if (!other_instance_uses_mesh(state, slot, entity)) {
            state.scene->Entity_Remove(entity);
            return;
        }
        state.scene->objects.Remove(entity);
        state.scene->transforms.Remove(entity);
        return;
    }
    state.scene->Entity_Remove(entity);
    if (!other_instance_uses_mesh(state, slot, source) &&
        state.scene->objects.GetComponent(source) == nullptr) {
        state.scene->Entity_Remove(source);
    }
}

extern "C" int64_t elisa_render_scene_v1_create_mesh_instance(
    int64_t source_handle,
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
    size_t source_slot = MAX_INSTANCES;
    if (!valid_handle(state, source_handle, source_slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    const InstanceSlot& source = state.instances[source_slot];
    if (!shared_source_static(state, source)) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    const size_t slot = find_free_slot(state);
    if (slot == MAX_INSTANCES) return ELISA_RENDER_SCENE_CAPACITY;
    InstanceSlot& instance = state.instances[slot];
    if (instance.generation >= MAX_GENERATION) return ELISA_RENDER_SCENE_GENERATION_EXHAUSTED;
    clear_snapshot_instance(instance);
    const uint64_t generation = instance.generation + 1;
    const std::string name = "elisa_mesh_instance_" + std::to_string(slot) + "_" + std::to_string(generation);

    wi::ecs::Entity entity = wi::ecs::INVALID_ENTITY;
    try {
        entity = state.scene->Entity_CreateObject(name);
        if (entity == wi::ecs::INVALID_ENTITY) return ELISA_RENDER_SCENE_BACKEND_FAILED;
        wi::scene::TransformComponent* transform = state.scene->transforms.GetComponent(entity);
        wi::scene::ObjectComponent* object = state.scene->objects.GetComponent(entity);
        const wi::scene::ObjectComponent* source_object = state.scene->objects.GetComponent(source.entity);
        if (transform == nullptr || object == nullptr || source_object == nullptr) {
            state.scene->Entity_Remove(entity);
            return ELISA_RENDER_SCENE_BACKEND_FAILED;
        }
        elisa::rendering::set_transform(*transform, px, py, pz, qx, qy, qz, qw, sx, sy, sz);
        object->meshID = source.entity;
        object->color = XMFLOAT4(red, green, blue, alpha);
        object->SetCastShadow(source_object->IsCastingShadow());
    } catch (...) {
        if (entity != wi::ecs::INVALID_ENTITY) state.scene->Entity_Remove(entity);
        return ELISA_RENDER_SCENE_BACKEND_FAILED;
    }

    instance.entity = entity;
    instance.shared_source = source.entity;
    instance.generation = generation;
    instance.live = true;
    return int64_t(encode_handle(slot, generation));
}
