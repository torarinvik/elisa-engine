constexpr float SNAPSHOT_DEFAULT_RED = 0.8f;
constexpr float SNAPSHOT_DEFAULT_GREEN = 0.8f;
constexpr float SNAPSHOT_DEFAULT_BLUE = 0.85f;
constexpr float SNAPSHOT_DEFAULT_ALPHA = 1.0f;

void clear_snapshot_instance(InstanceSlot& instance) {
    instance.entity = wi::ecs::INVALID_ENTITY;
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
    instance.live = false;
}

void discard_snapshot_entities(RenderSceneService& state) {
    if (state.scene == nullptr) return;
    for (size_t index = 0; index < state.snapshot_row_count; ++index) {
        SnapshotStageRow& row = state.snapshot_rows[index];
        if (row.created_entity != wi::ecs::INVALID_ENTITY) {
            state.scene->Entity_Remove(row.created_entity);
            row.created_entity = wi::ecs::INVALID_ENTITY;
        }
    }
}

void close_snapshot_transaction(RenderSceneService& state, bool keep_results) {
    state.snapshot_transaction_active = false;
    state.snapshot_row_count = 0;
    state.snapshot_retire_count = 0;
    state.snapshot_expected_previous_count = 0;
    state.snapshot_rows = {};
    state.snapshot_retire_handles = {};
    if (!keep_results) {
        state.snapshot_result_count = 0;
        state.snapshot_results = {};
    }
}

int32_t fail_snapshot_transaction(RenderSceneService& state, int32_t status) {
    discard_snapshot_entities(state);
    close_snapshot_transaction(state, false);
    return status;
}

bool snapshot_assets_valid(const SnapshotStageRow& row) {
    const bool mesh_valid = row.mesh_high != 0 || row.mesh_low != 0;
    const bool material_valid = row.material_high != 0 || row.material_low != 0;
    const bool same_asset = row.mesh_high == row.material_high && row.mesh_low == row.material_low;
    return mesh_valid && material_valid && !same_asset;
}

wi::ecs::Entity create_snapshot_entity(RenderSceneService& state, const SnapshotStageRow& row,
    size_t slot, uint64_t generation) {
    wi::ecs::Entity entity = wi::ecs::INVALID_ENTITY;
    try {
        const std::string name = "elisa_snapshot_" + std::to_string(row.render_id) + "_" +
            std::to_string(slot) + "_" + std::to_string(generation);
        entity = state.scene->Entity_CreateCube(name);
        if (entity == wi::ecs::INVALID_ENTITY) return entity;
        wi::scene::TransformComponent* transform = state.scene->transforms.GetComponent(entity);
        wi::scene::MaterialComponent* material = state.scene->materials.GetComponent(entity);
        wi::scene::ObjectComponent* object = state.scene->objects.GetComponent(entity);
        if (transform == nullptr || material == nullptr || object == nullptr) {
            state.scene->Entity_Remove(entity);
            return wi::ecs::INVALID_ENTITY;
        }
        apply_transform(*transform,
            row.position[0], row.position[1], row.position[2],
            row.rotation[0], row.rotation[1], row.rotation[2], row.rotation[3],
            row.scale[0], row.scale[1], row.scale[2]);
        material->shaderType = wi::scene::MaterialComponent::SHADERTYPE_UNLIT;
        material->SetBaseColor(XMFLOAT4(SNAPSHOT_DEFAULT_RED, SNAPSHOT_DEFAULT_GREEN,
            SNAPSHOT_DEFAULT_BLUE, SNAPSHOT_DEFAULT_ALPHA));
        material->SetCastShadow(false);
        object->SetCastShadow(false);
        return entity;
    } catch (...) {
        if (entity != wi::ecs::INVALID_ENTITY) state.scene->Entity_Remove(entity);
        return wi::ecs::INVALID_ENTITY;
    }
}

bool snapshot_handle_is_retired(const RenderSceneService& state, int64_t handle) {
    for (size_t index = 0; index < state.snapshot_retire_count; ++index) {
        if (state.snapshot_retire_handles[index] == handle) return true;
    }
    return false;
}
