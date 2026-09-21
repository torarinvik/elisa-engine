extern "C" int32_t elisa_render_scene_v1_snapshot_begin(uint32_t previous_count) {
    if (previous_count > MAX_INSTANCES) return ELISA_RENDER_SCENE_CAPACITY;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (state.snapshot_transaction_active) return ELISA_RENDER_SCENE_BATCH_ACTIVE;
    close_snapshot_transaction(state, false);
    state.snapshot_expected_previous_count = previous_count;
    state.snapshot_transaction_active = true;
#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
    state.snapshot_test_transaction_api_calls = 1;
#endif
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_snapshot_stage(
    int64_t existing_handle, int64_t gameplay_epoch, int64_t gameplay_id, int64_t render_id,
    uint64_t mesh_high, uint64_t mesh_low, uint64_t material_high, uint64_t material_low,
    float px, float py, float pz, float qx, float qy, float qz, float qw,
    float sx, float sy, float sz) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (!state.snapshot_transaction_active || existing_handle < 0 || gameplay_epoch <= 0 ||
        gameplay_id <= 0 || render_id <= 0 || !valid_transform(px, py, pz, qx, qy, qz, qw, sx, sy, sz)) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    if (state.snapshot_row_count >= MAX_INSTANCES) return ELISA_RENDER_SCENE_CAPACITY;
    SnapshotStageRow row{};
    row.existing_handle = existing_handle;
    row.gameplay_epoch = gameplay_epoch;
    row.gameplay_id = gameplay_id;
    row.render_id = render_id;
    row.mesh_high = mesh_high;
    row.mesh_low = mesh_low;
    row.material_high = material_high;
    row.material_low = material_low;
    row.position[0] = px; row.position[1] = py; row.position[2] = pz;
    row.rotation[0] = qx; row.rotation[1] = qy; row.rotation[2] = qz; row.rotation[3] = qw;
    row.scale[0] = sx; row.scale[1] = sy; row.scale[2] = sz;
    if (!snapshot_assets_valid(row)) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    // A retained row is checked against its instance first, so a changed
    // mesh or material is refused even when that asset isn't resident.
    if (existing_handle != 0) {
        size_t slot = MAX_INSTANCES;
        if (!valid_handle(state, existing_handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
        if (!snapshot_row_retains_instance(row, state.instances[slot])) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
        if (state.scene->transforms.GetComponent(state.instances[slot].entity) == nullptr) {
            return ELISA_RENDER_SCENE_BACKEND_FAILED;
        }
    }
    const int32_t asset_status = snapshot_row_asset_status(state, mesh_high, mesh_low, material_high, material_low);
    if (asset_status != ELISA_RENDER_SCENE_OK) return asset_status;
    for (size_t index = 0; index < state.snapshot_row_count; ++index) {
        if (state.snapshot_rows[index].render_id == render_id ||
            (existing_handle != 0 && state.snapshot_rows[index].existing_handle == existing_handle)) {
            return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
        }
    }
    state.snapshot_rows[state.snapshot_row_count++] = row;
#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
    ++state.snapshot_test_transaction_api_calls;
#endif
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_snapshot_retire(int64_t handle) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (!state.snapshot_transaction_active || handle <= 0) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    if (state.snapshot_retire_count >= MAX_INSTANCES) return ELISA_RENDER_SCENE_CAPACITY;
    size_t slot = MAX_INSTANCES;
    if (!valid_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    for (size_t index = 0; index < state.snapshot_retire_count; ++index) {
        if (state.snapshot_retire_handles[index] == handle) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    for (size_t index = 0; index < state.snapshot_row_count; ++index) {
        if (state.snapshot_rows[index].existing_handle == handle) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    state.snapshot_retire_handles[state.snapshot_retire_count++] = handle;
#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
    ++state.snapshot_test_transaction_api_calls;
#endif
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_snapshot_commit(void) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (!state.snapshot_transaction_active) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    size_t accounted_previous_handles = state.snapshot_retire_count;
    for (size_t index = 0; index < state.snapshot_row_count; ++index) {
        accounted_previous_handles += state.snapshot_rows[index].existing_handle != 0 ? 1 : 0;
    }
    if (accounted_previous_handles != state.snapshot_expected_previous_count) {
        return fail_snapshot_transaction(state, ELISA_RENDER_SCENE_INVALID_ARGUMENT);
    }

    std::array<bool, MAX_INSTANCES> retained_slots{};
    std::array<bool, MAX_INSTANCES> reserved_slots{};
    size_t new_row_count = 0;
    for (size_t index = 0; index < state.snapshot_row_count; ++index) {
        SnapshotStageRow& row = state.snapshot_rows[index];
        if (row.existing_handle == 0) {
            ++new_row_count;
            continue;
        }
        size_t slot = MAX_INSTANCES;
        if (!valid_handle(state, row.existing_handle, slot) ||
            state.scene->transforms.GetComponent(state.instances[slot].entity) == nullptr ||
            state.scene->objects.GetComponent(state.instances[slot].entity) == nullptr) {
            return fail_snapshot_transaction(state, ELISA_RENDER_SCENE_UNKNOWN_HANDLE);
        }
        retained_slots[slot] = true;
    }

    for (size_t index = 0; index < state.snapshot_retire_count; ++index) {
        const int64_t handle = state.snapshot_retire_handles[index];
        size_t slot = MAX_INSTANCES;
        if (!valid_handle(state, handle, slot)) {
            return fail_snapshot_transaction(state, ELISA_RENDER_SCENE_UNKNOWN_HANDLE);
        }
        if (retained_slots[slot]) return fail_snapshot_transaction(state, ELISA_RENDER_SCENE_INVALID_ARGUMENT);
        for (size_t earlier = 0; earlier < index; ++earlier) {
            if (state.snapshot_retire_handles[earlier] == handle) {
                return fail_snapshot_transaction(state, ELISA_RENDER_SCENE_INVALID_ARGUMENT);
            }
        }
    }

    for (size_t index = 0; index < state.snapshot_row_count; ++index) {
        SnapshotStageRow& row = state.snapshot_rows[index];
        if (row.existing_handle != 0) continue;
        size_t chosen = MAX_INSTANCES;
        for (size_t retired = 0; retired < state.snapshot_retire_count; ++retired) {
            size_t candidate = MAX_INSTANCES;
            if (!valid_handle(state, state.snapshot_retire_handles[retired], candidate) || reserved_slots[candidate]) continue;
            if (state.instances[candidate].render_id == row.render_id) {
                chosen = candidate;
                break;
            }
        }
        if (chosen == MAX_INSTANCES) {
            for (size_t retired = 0; retired < state.snapshot_retire_count; ++retired) {
                size_t candidate = MAX_INSTANCES;
                if (!valid_handle(state, state.snapshot_retire_handles[retired], candidate) || reserved_slots[candidate]) continue;
                chosen = candidate;
                break;
            }
        }
        if (chosen == MAX_INSTANCES) {
            for (size_t candidate = 0; candidate < MAX_INSTANCES; ++candidate) {
                if (!state.instances[candidate].live && !reserved_slots[candidate]) {
                    chosen = candidate;
                    break;
                }
            }
        }
        if (chosen == MAX_INSTANCES) return fail_snapshot_transaction(state, ELISA_RENDER_SCENE_CAPACITY);
        if (state.instances[chosen].generation >= MAX_GENERATION) {
            return fail_snapshot_transaction(state, ELISA_RENDER_SCENE_GENERATION_EXHAUSTED);
        }
        reserved_slots[chosen] = true;
        row.reserved_slot = chosen;
        row.reserved_generation = state.instances[chosen].generation + 1;
    }
    if (new_row_count > MAX_INSTANCES) return fail_snapshot_transaction(state, ELISA_RENDER_SCENE_CAPACITY);

    for (size_t index = 0; index < state.snapshot_row_count; ++index) {
        SnapshotStageRow& row = state.snapshot_rows[index];
        if (row.existing_handle != 0) continue;
        if (state.snapshot_test_fail_after_creates == 0) {
            state.snapshot_test_fail_after_creates = -1;
            return fail_snapshot_transaction(state, ELISA_RENDER_SCENE_BACKEND_FAILED);
        }
        int32_t create_status = ELISA_RENDER_SCENE_BACKEND_FAILED;
        row.created_entity = create_snapshot_entity(state, row,
            row.reserved_slot, row.reserved_generation, create_status);
        if (row.created_entity == wi::ecs::INVALID_ENTITY) {
            return fail_snapshot_transaction(state, create_status);
        }
        if (state.snapshot_test_fail_after_creates > 0) --state.snapshot_test_fail_after_creates;
    }

    // Retain newly created static mesh pairs before retiring their predecessors.
    // A replacement may use the same shared pair as the object it replaces.
    std::array<size_t, MAX_SNAPSHOT_SHARED_MESHES> shared_mesh_additions{};
    for (size_t index = 0; index < state.snapshot_row_count; ++index) {
        SnapshotStageRow& row = state.snapshot_rows[index];
        if (row.existing_handle != 0 || row.shared_mesh_slot >= MAX_SNAPSHOT_SHARED_MESHES) continue;
        SnapshotSharedMesh& shared = state.snapshot_shared_meshes[row.shared_mesh_slot];
        if (!shared.live || shared_mesh_additions[row.shared_mesh_slot] == std::numeric_limits<size_t>::max()) {
            return fail_snapshot_transaction(state, ELISA_RENDER_SCENE_BACKEND_FAILED);
        }
        ++shared_mesh_additions[row.shared_mesh_slot];
    }
    for (size_t index = 0; index < MAX_SNAPSHOT_SHARED_MESHES; ++index) {
        SnapshotSharedMesh& shared = state.snapshot_shared_meshes[index];
        if (shared_mesh_additions[index] > std::numeric_limits<size_t>::max() - shared.references) {
            return fail_snapshot_transaction(state, ELISA_RENDER_SCENE_BACKEND_FAILED);
        }
        shared.references += shared_mesh_additions[index];
    }

    for (size_t index = 0; index < state.snapshot_row_count; ++index) {
        SnapshotStageRow& row = state.snapshot_rows[index];
        if (row.existing_handle == 0) continue;
        size_t slot = MAX_INSTANCES;
        valid_handle(state, row.existing_handle, slot);
        wi::scene::TransformComponent* transform = state.scene->transforms.GetComponent(state.instances[slot].entity);
        apply_transform(*transform,
            row.position[0], row.position[1], row.position[2],
            row.rotation[0], row.rotation[1], row.rotation[2], row.rotation[3],
            row.scale[0], row.scale[1], row.scale[2]);
    }

    for (size_t index = 0; index < state.snapshot_retire_count; ++index) {
        size_t slot = MAX_INSTANCES;
        valid_handle(state, state.snapshot_retire_handles[index], slot);
        state.scene->Entity_Remove(state.instances[slot].entity);
        for (wi::ecs::Entity joint : state.instances[slot].joint_entities) state.scene->Entity_Remove(joint);
        if (state.instances[slot].shared_mesh_slot < MAX_SNAPSHOT_SHARED_MESHES) {
            release_snapshot_shared_mesh(state, state.instances[slot].shared_mesh_slot);
        }
        clear_snapshot_instance(state.instances[slot]);
    }

    for (size_t index = 0; index < state.snapshot_row_count; ++index) {
        SnapshotStageRow& row = state.snapshot_rows[index];
        size_t slot = MAX_INSTANCES;
        uint64_t generation = 0;
        if (row.existing_handle != 0) {
            valid_handle(state, row.existing_handle, slot);
            generation = state.instances[slot].generation;
        } else {
            slot = row.reserved_slot;
            generation = row.reserved_generation;
            InstanceSlot& instance = state.instances[slot];
            clear_snapshot_instance(instance);
            instance.generation = generation;
            instance.entity = row.created_entity;
            row.created_entity = wi::ecs::INVALID_ENTITY;
            instance.shared_mesh_slot = row.shared_mesh_slot;
            instance.joint_entities = std::move(row.created_joint_entities);
            instance.skin_joints = std::move(row.created_skin_joints);
            instance.animation_clips = std::move(row.created_animation_clips);
            instance.live = true;
        }
        InstanceSlot& instance = state.instances[slot];
        instance.gameplay_epoch = row.gameplay_epoch;
        instance.gameplay_id = row.gameplay_id;
        instance.render_id = row.render_id;
        instance.mesh_high = row.mesh_high;
        instance.mesh_low = row.mesh_low;
        instance.material_high = row.material_high;
        instance.material_low = row.material_low;
        // Every committed row carries its tint, so dropping an override resets it.
        wi::scene::ObjectComponent* object = state.scene->objects.GetComponent(instance.entity);
        object->color = XMFLOAT4(row.tint_color[0], row.tint_color[1], row.tint_color[2], 1.0f);
        object->emissiveColor = XMFLOAT4(row.tint_emissive[0], row.tint_emissive[1],
            row.tint_emissive[2], row.tint_emissive[3]);
        state.snapshot_results[index] = int64_t(encode_handle(slot, generation));
    }

    state.snapshot_result_count = state.snapshot_row_count;
#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
    state.snapshot_test_last_transaction_api_calls = state.snapshot_test_transaction_api_calls + 1;
#endif
    state.snapshot_transaction_active = false;
    state.snapshot_row_count = 0;
    state.snapshot_retire_count = 0;
    state.snapshot_expected_previous_count = 0;
    state.snapshot_rows = {};
    state.snapshot_retire_handles = {};
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_snapshot_abort(void) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (state.snapshot_transaction_active) return fail_snapshot_transaction(state, ELISA_RENDER_SCENE_OK);
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int64_t elisa_render_scene_v1_snapshot_result(uint32_t index) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.snapshot_transaction_active ||
        index >= state.snapshot_result_count) return -1;
    return state.snapshot_results[index];
}

extern "C" int32_t elisa_render_scene_v1_register_snapshot_mesh_asset(
    uint64_t high, uint64_t low, const char* package_path) {
    if ((high == 0 && low == 0) || package_path == nullptr) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (state.snapshot_transaction_active) return ELISA_RENDER_SCENE_BATCH_ACTIVE;
    std::filesystem::path resolved_path;
    if (!elisa::assets::resolve_project_asset_path(package_path, resolved_path)) {
        return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
    }
    const size_t existing = snapshot_mesh_asset_slot(state, high, low);
    if (existing < MAX_SNAPSHOT_MESH_ASSETS) {
        return state.snapshot_mesh_assets[existing].canonical_path == resolved_path
            ? ELISA_RENDER_SCENE_OK : ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    const int32_t claim = claim_snapshot_asset_id(state, SnapshotAssetKind::Mesh, high, low);
    if (claim != ELISA_RENDER_SCENE_OK) return claim;
    size_t free_slot = MAX_SNAPSHOT_MESH_ASSETS;
    for (size_t index = 0; index < MAX_SNAPSHOT_MESH_ASSETS; ++index) {
        if (!state.snapshot_mesh_assets[index].live) {
            free_slot = index;
            break;
        }
    }
    if (free_slot == MAX_SNAPSHOT_MESH_ASSETS) return ELISA_RENDER_SCENE_CAPACITY;
    elisa::assets::CookedGeometry geometry;
    std::string load_error;
    try {
        if (!elisa::assets::verify_bundle_dependencies(resolved_path, load_error) ||
            !elisa::assets::load_cooked_geometry_asset(resolved_path.string(), geometry, load_error)) {
            std::fprintf(stderr, "Elisa snapshot mesh load failed: %s\n", load_error.c_str());
            return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Elisa snapshot mesh load exception: %s\n", error.what());
        return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
    } catch (...) {
        return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
    }
    size_t bytes = 0;
    if (!cooked_geometry_bytes(geometry, bytes)) return ELISA_RENDER_SCENE_CAPACITY;
    return install_snapshot_mesh_asset(state, high, low, package_path,
        std::move(resolved_path), std::move(geometry), bytes);
}

extern "C" int32_t elisa_render_scene_v1_unregister_snapshot_mesh_asset(uint64_t high, uint64_t low) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (state.snapshot_transaction_active) return ELISA_RENDER_SCENE_BATCH_ACTIVE;
    const size_t slot = snapshot_mesh_asset_slot(state, high, low);
    if (slot == MAX_SNAPSHOT_MESH_ASSETS) return cancel_snapshot_asset_request(state, SnapshotAssetKind::Mesh, high, low);
    for (const InstanceSlot& instance : state.instances) {
        if (instance.live && instance.mesh_high == high && instance.mesh_low == low) {
            return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
        }
    }
    for (const SnapshotSharedMesh& shared : state.snapshot_shared_meshes) {
        if (shared.live && shared.mesh_high == high && shared.mesh_low == low) {
            return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
        }
    }
    state.snapshot_geometry_bytes -= state.snapshot_mesh_assets[slot].bytes;
    state.snapshot_mesh_assets[slot] = SnapshotMeshAssetSlot{};
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_register_snapshot_texture_asset(
    uint64_t high, uint64_t low, const char* asset_path) {
    if (high == 0 && low == 0) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (state.snapshot_transaction_active) return ELISA_RENDER_SCENE_BATCH_ACTIVE;
    std::filesystem::path resolved_path;
    try {
        if (!elisa::assets::resolve_project_asset_path(asset_path, resolved_path)) {
            return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
        }
    } catch (...) {
        return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
    }
    const size_t existing = snapshot_texture_asset_slot(state, high, low);
    if (existing < MAX_SNAPSHOT_TEXTURE_ASSETS) {
        const SnapshotTextureAssetSlot& asset = state.snapshot_texture_assets[existing];
        return asset.canonical_path == resolved_path && asset.section.empty()
            ? ELISA_RENDER_SCENE_OK : ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    const int32_t claim = claim_snapshot_asset_id(state, SnapshotAssetKind::Texture, high, low);
    if (claim != ELISA_RENDER_SCENE_OK) return claim;
    size_t free_slot = MAX_SNAPSHOT_TEXTURE_ASSETS;
    for (size_t index = 0; index < MAX_SNAPSHOT_TEXTURE_ASSETS; ++index) {
        if (!state.snapshot_texture_assets[index].live) {
            free_slot = index;
            break;
        }
    }
    if (free_slot == MAX_SNAPSHOT_TEXTURE_ASSETS) return ELISA_RENDER_SCENE_CAPACITY;
    SnapshotTextureAssetSlot& asset = state.snapshot_texture_assets[free_slot];
    asset.high = high;
    asset.low = low;
    asset.canonical_path = std::move(resolved_path);
    asset.live = true;
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_unregister_snapshot_texture_asset(uint64_t high, uint64_t low) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (state.snapshot_transaction_active) return ELISA_RENDER_SCENE_BATCH_ACTIVE;
    const size_t slot = snapshot_texture_asset_slot(state, high, low);
    if (slot == MAX_SNAPSHOT_TEXTURE_ASSETS) return cancel_snapshot_asset_request(state, SnapshotAssetKind::Texture, high, low);
    for (const SnapshotMaterialAssetSlot& material : state.snapshot_material_assets) {
        if (!material.live) continue;
        for (size_t texture_slot = 0; texture_slot < SNAPSHOT_MATERIAL_TEXTURE_SLOTS; ++texture_slot) {
            if (material.texture_high[texture_slot] == high && material.texture_low[texture_slot] == low) {
                return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
            }
        }
    }
    state.snapshot_bundle_texture_source_bytes -= state.snapshot_texture_assets[slot].source_bytes;
    state.snapshot_decoded_texture_bytes -= state.snapshot_texture_assets[slot].decoded.pixels.size();
    state.snapshot_texture_assets[slot] = SnapshotTextureAssetSlot{};
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_register_snapshot_material_asset_with_textures(
    uint64_t high, uint64_t low,
    uint64_t base_color_high, uint64_t base_color_low,
    uint64_t normal_high, uint64_t normal_low,
    uint64_t surface_high, uint64_t surface_low,
    uint64_t emissive_high, uint64_t emissive_low,
    float red, float green, float blue, float alpha,
    float metallic, float roughness,
    float emissive_red, float emissive_green, float emissive_blue,
    float alpha_cutoff, int32_t alpha_mode, int32_t double_sided) {
    SnapshotMaterialAssetSlot values;
    values.base_color[0] = red; values.base_color[1] = green;
    values.base_color[2] = blue; values.base_color[3] = alpha;
    values.metallic = metallic;
    values.roughness = roughness;
    values.emissive[0] = emissive_red; values.emissive[1] = emissive_green; values.emissive[2] = emissive_blue;
    values.alpha_cutoff = alpha_cutoff;
    values.alpha_mode = alpha_mode;
    values.double_sided = double_sided != 0;
    values.texture_high = {base_color_high, normal_high, surface_high, emissive_high};
    values.texture_low = {base_color_low, normal_low, surface_low, emissive_low};
    if ((high == 0 && low == 0) || !snapshot_material_values_valid(values) ||
        (double_sided != 0 && double_sided != 1)) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (state.snapshot_transaction_active) return ELISA_RENDER_SCENE_BATCH_ACTIVE;
    bool created = false;
    return register_snapshot_material_locked(state, high, low, values, created);
}

extern "C" int32_t elisa_render_scene_v1_register_snapshot_material_asset(
    uint64_t high, uint64_t low,
    float red, float green, float blue, float alpha,
    float metallic, float roughness,
    float emissive_red, float emissive_green, float emissive_blue,
    float alpha_cutoff, int32_t alpha_mode, int32_t double_sided) {
    return elisa_render_scene_v1_register_snapshot_material_asset_with_textures(
        high, low, 0, 0, 0, 0, 0, 0, 0, 0,
        red, green, blue, alpha, metallic, roughness,
        emissive_red, emissive_green, emissive_blue, alpha_cutoff, alpha_mode, double_sided);
}

extern "C" int32_t elisa_render_scene_v1_unregister_snapshot_material_asset(uint64_t high, uint64_t low) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (state.snapshot_transaction_active) return ELISA_RENDER_SCENE_BATCH_ACTIVE;
    const size_t slot = snapshot_material_asset_slot(state, high, low);
    if (slot == MAX_SNAPSHOT_MATERIAL_ASSETS) return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
    if (snapshot_material_id_in_use(state, high, low) || snapshot_material_listed_by_set(state, high, low)) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    remove_snapshot_material(state, slot);
    return ELISA_RENDER_SCENE_OK;
}

#include "render_scene_snapshot_material_set_abi.inc"
#include "render_scene_snapshot_mesh_material_abi.inc"
