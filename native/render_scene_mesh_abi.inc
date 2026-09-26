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
    instance.morph_default_weights.clear();
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
        const bool has_placement_metadata = !geometry.mesh_placements.empty();
        const bool configured = has_placement_metadata ?
            elisa::rendering::configure_cooked_mesh(*state.scene, entity, geometry,
                px, py, pz, qx, qy, qz, qw, sx, sy, sz, red, green, blue, alpha,
                &joint_entities, 0) :
            elisa::rendering::configure_cooked_mesh(*state.scene, entity, geometry,
                px, py, pz, qx, qy, qz, qw, sx, sy, sz, red, green, blue, alpha,
                &joint_entities);
        if (!configured) {
            state.scene->Entity_Remove(entity);
            return ELISA_RENDER_SCENE_BACKEND_FAILED;
        }
        if (has_placement_metadata) {
            placement_entities.reserve(geometry.mesh_placements.size() - 1);
            const wi::ecs::Entity placement_armature = geometry.skin_joints.empty() ||
                geometry.skin_cluster_joints.empty() ? wi::ecs::INVALID_ENTITY : entity;
            for (size_t placement = 1; placement < geometry.mesh_placements.size(); ++placement) {
                const std::string child_name = name + "_placement_" + std::to_string(placement);
                const wi::ecs::Entity child = state.scene->Entity_CreateCube(child_name);
                if (child == wi::ecs::INVALID_ENTITY ||
                    !elisa::rendering::configure_cooked_mesh(*state.scene, child, geometry,
                        0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
                        1.0f, 1.0f, 1.0f, red, green, blue, alpha,
                        nullptr, placement, placement_armature)) {
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
            release_imported_scene(state, instance);
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
    instance.morph_default_weights = std::move(geometry.morph_default_weights);
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
        !source.imported_mesh_entities.empty() || !source.snapshot_placement_entities.empty()) return false;
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
    clear_snapshot_instance(state, instance);
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

// Generate shared, material-preserving static mesh LODs once at load time.
// Physics and instance transforms remain independent of these render indices.
constexpr int32_t MIN_STATIC_LOD_LEVELS = 2;
constexpr int32_t MAX_STATIC_LOD_LEVELS = 4;
constexpr float MIN_STATIC_LOD_RATIO = 0.1f;
constexpr float MAX_STATIC_LOD_RATIO = 0.8f;
constexpr float MAX_STATIC_LOD_ERROR = 0.05f;
extern "C" int32_t elisa_render_scene_v1_generate_static_lods(int64_t handle,
    int32_t levels, float ratio, float error) {
    if (levels < MIN_STATIC_LOD_LEVELS || levels > MAX_STATIC_LOD_LEVELS ||
        !std::isfinite(ratio) || ratio < MIN_STATIC_LOD_RATIO || ratio > MAX_STATIC_LOD_RATIO ||
        !std::isfinite(error) || error <= 0 || error > MAX_STATIC_LOD_ERROR)
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    auto& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    if (!shared_source_static(state, state.instances[slot])) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    auto& mesh = *state.scene->meshes.GetComponent(state.instances[slot].entity);
    constexpr size_t MAX_INDEX_VALUE = std::numeric_limits<uint32_t>::max();
    if (mesh.subsets_per_lod != 0 || mesh.indices.empty() || mesh.vertex_positions.empty() ||
        mesh.subsets.empty() || mesh.indices.size() % 3 != 0 ||
        mesh.indices.size() > MAX_INDEX_VALUE || mesh.vertex_positions.size() > MAX_INDEX_VALUE ||
        mesh.subsets.size() > MAX_INDEX_VALUE / size_t(levels))
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    size_t additional_indices = 0;
    for (const auto& subset : mesh.subsets) {
        if (subset.indexCount == 0 || subset.indexCount % 3 != 0 ||
            subset.indexOffset > mesh.indices.size() ||
            subset.indexCount > mesh.indices.size() - subset.indexOffset ||
            additional_indices > MAX_INDEX_VALUE - size_t(subset.indexCount) * size_t(levels - 1))
            return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
        additional_indices += size_t(subset.indexCount) * size_t(levels - 1);
    }
    if (additional_indices > MAX_INDEX_VALUE - mesh.indices.size())
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    for (const XMFLOAT3& position : mesh.vertex_positions) {
        if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z))
            return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    for (uint32_t index : mesh.indices) {
        if (index >= mesh.vertex_positions.size()) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    auto indices = mesh.indices;
    auto subsets = mesh.subsets;
    const uint32_t count = uint32_t(subsets.size());
    try {
        for (int32_t level = 1; level < levels; ++level) {
            for (uint32_t part = 0; part < count; ++part) {
                auto subset = mesh.subsets[part];
                const auto* source = mesh.indices.data() + subset.indexOffset;
                wi::vector<uint32_t> reduced(subset.indexCount);
                if (subset.indexCount > 0) {
                    const size_t target = std::max(size_t(3), size_t(subset.indexCount *
                        std::pow(ratio, level)) / 3 * 3);
                    const size_t actual = meshopt_simplify(reduced.data(), source, subset.indexCount,
                        &mesh.vertex_positions[0].x, mesh.vertex_positions.size(), sizeof(XMFLOAT3),
                        target, error);
                    reduced.resize(actual);
                    // Never erase a material region, even on very small meshes.
                    if (reduced.empty()) reduced.assign(source, source + subset.indexCount);
                }
                subset.indexOffset = uint32_t(indices.size());
                subset.indexCount = uint32_t(reduced.size());
                indices.insert(indices.end(), reduced.begin(), reduced.end());
                subsets.push_back(subset);
            }
        }
        mesh.indices.swap(indices);
        mesh.subsets.swap(subsets);
        mesh.subsets_per_lod = count;
        mesh.CreateRenderData();
    } catch (...) {
        // Preserve the source data if GPU resource creation fails.
        if (mesh.subsets_per_lod != 0) {
            mesh.indices.swap(indices); mesh.subsets.swap(subsets); mesh.subsets_per_lod = 0;
            try { mesh.CreateRenderData(); } catch (...) {}
        }
        return ELISA_RENDER_SCENE_BACKEND_FAILED;
    }
    std::fprintf(stderr, "Elisa static LOD: %zu base triangles, %d levels\n", indices.size()/3, levels);
#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
    state.static_lod_test_probe_handle = handle;
#endif
    return ELISA_RENDER_SCENE_OK;
}

#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
extern "C" int32_t elisa_render_scene_v1_test_static_lod_layout(int32_t expected_levels) {
    auto& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    size_t slot = MAX_INSTANCES;
    if (!state.initialized || !on_owner_thread(state) || expected_levels < MIN_STATIC_LOD_LEVELS ||
        expected_levels > MAX_STATIC_LOD_LEVELS ||
        !valid_handle(state, state.static_lod_test_probe_handle, slot)) return 0;
    const auto* mesh = state.scene->meshes.GetComponent(state.instances[slot].entity);
    if (mesh == nullptr || mesh->subsets_per_lod == 0 ||
        mesh->subsets.size() != size_t(mesh->subsets_per_lod) * size_t(expected_levels)) return 0;
    for (const auto& subset : mesh->subsets) {
        if (subset.indexCount == 0 || subset.indexCount % 3 != 0 ||
            subset.indexOffset > mesh->indices.size() ||
            subset.indexCount > mesh->indices.size() - subset.indexOffset) return 0;
        for (size_t index = subset.indexOffset;
            index < size_t(subset.indexOffset) + subset.indexCount; ++index) {
            if (mesh->indices[index] >= mesh->vertex_positions.size()) return 0;
        }
    }
    return 1;
}
#endif
