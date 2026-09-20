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
    for (size_t index = 0; index < state.snapshot_row_count; ++index) {
        if (state.snapshot_rows[index].render_id == render_id ||
            (existing_handle != 0 && state.snapshot_rows[index].existing_handle == existing_handle)) {
            return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
        }
    }
    if (existing_handle != 0) {
        size_t slot = MAX_INSTANCES;
        if (!valid_handle(state, existing_handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
        if (state.scene->transforms.GetComponent(state.instances[slot].entity) == nullptr) {
            return ELISA_RENDER_SCENE_BACKEND_FAILED;
        }
    }
    state.snapshot_rows[state.snapshot_row_count++] = row;
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
            state.scene->transforms.GetComponent(state.instances[slot].entity) == nullptr) {
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
        row.created_entity = create_snapshot_entity(state, row, row.reserved_slot, row.reserved_generation);
        if (row.created_entity == wi::ecs::INVALID_ENTITY) {
            return fail_snapshot_transaction(state, ELISA_RENDER_SCENE_BACKEND_FAILED);
        }
        if (state.snapshot_test_fail_after_creates > 0) --state.snapshot_test_fail_after_creates;
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
        state.snapshot_results[index] = int64_t(encode_handle(slot, generation));
    }

    state.snapshot_result_count = state.snapshot_row_count;
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
