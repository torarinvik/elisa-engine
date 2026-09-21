size_t snapshot_mesh_asset_slot(const RenderSceneService& state, uint64_t high, uint64_t low) {
    for (size_t index = 0; index < MAX_SNAPSHOT_MESH_ASSETS; ++index) {
        const SnapshotMeshAssetSlot& asset = state.snapshot_mesh_assets[index];
        if (asset.live && asset.high == high && asset.low == low) return index;
    }
    return MAX_SNAPSHOT_MESH_ASSETS;
}

size_t snapshot_material_asset_slot(const RenderSceneService& state, uint64_t high, uint64_t low) {
    for (size_t index = 0; index < MAX_SNAPSHOT_MATERIAL_ASSETS; ++index) {
        const SnapshotMaterialAssetSlot& asset = state.snapshot_material_assets[index];
        if (asset.live && asset.high == high && asset.low == low) return index;
    }
    return MAX_SNAPSHOT_MATERIAL_ASSETS;
}

bool add_geometry_bytes(size_t count, size_t element_bytes, size_t& total) {
    if (element_bytes != 0 && count > (MAX_SNAPSHOT_GEOMETRY_BYTES - total) / element_bytes) return false;
    total += count * element_bytes;
    return true;
}

bool cooked_geometry_bytes(const elisa::assets::CookedGeometry& geometry, size_t& bytes) {
    size_t total = 0;
    if (!add_geometry_bytes(geometry.positions.size(), sizeof(float), total) ||
        !add_geometry_bytes(geometry.normals.size(), sizeof(float), total) ||
        !add_geometry_bytes(geometry.uvs.size(), sizeof(float), total) ||
        !add_geometry_bytes(geometry.tangents.size(), sizeof(float), total) ||
        !add_geometry_bytes(geometry.indices.size(), sizeof(uint32_t), total) ||
        !add_geometry_bytes(geometry.skin_indices.size(), sizeof(uint32_t), total) ||
        !add_geometry_bytes(geometry.skin_weights.size(), sizeof(float), total) ||
        !add_geometry_bytes(geometry.skin_cluster_joints.size(), sizeof(uint32_t), total)) return false;
    for (const std::string& name : geometry.skin_bone_names) {
        if (!add_geometry_bytes(name.size(), 1, total)) return false;
    }
    for (const auto& joint : geometry.skin_joints) {
        if (!add_geometry_bytes(joint.name.size(), 1, total) ||
            !add_geometry_bytes(joint.rest_local.size(), sizeof(float), total)) return false;
    }
    for (const auto& clip : geometry.animation_clips) {
        if (!add_geometry_bytes(clip.name.size(), 1, total) ||
            !add_geometry_bytes(clip.local_transforms.size(), sizeof(float), total)) return false;
    }
    bytes = total;
    return true;
}

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
    std::vector<wi::ecs::Entity> joint_entities;
    try {
        const size_t mesh_slot = snapshot_mesh_asset_slot(state, row.mesh_high, row.mesh_low);
        const size_t material_slot = snapshot_material_asset_slot(state, row.material_high, row.material_low);
        if (mesh_slot == MAX_SNAPSHOT_MESH_ASSETS || material_slot == MAX_SNAPSHOT_MATERIAL_ASSETS) return entity;
        const SnapshotMeshAssetSlot& mesh_asset = state.snapshot_mesh_assets[mesh_slot];
        const SnapshotMaterialAssetSlot& material_asset = state.snapshot_material_assets[material_slot];
        const std::string name = "elisa_snapshot_" + std::to_string(row.render_id) + "_" +
            std::to_string(slot) + "_" + std::to_string(generation);
        entity = state.scene->Entity_CreateCube(name);
        if (entity == wi::ecs::INVALID_ENTITY) return entity;
        if (!elisa::rendering::configure_cooked_mesh(*state.scene, entity, mesh_asset.geometry,
            row.position[0], row.position[1], row.position[2],
            row.rotation[0], row.rotation[1], row.rotation[2], row.rotation[3],
            row.scale[0], row.scale[1], row.scale[2],
            material_asset.base_color[0], material_asset.base_color[1],
            material_asset.base_color[2], material_asset.base_color[3], &joint_entities)) {
            for (wi::ecs::Entity joint : joint_entities) state.scene->Entity_Remove(joint);
            state.scene->Entity_Remove(entity);
            return wi::ecs::INVALID_ENTITY;
        }
        wi::scene::MaterialComponent* material = state.scene->materials.GetComponent(entity);
        wi::scene::ObjectComponent* object = state.scene->objects.GetComponent(entity);
        if (material == nullptr || object == nullptr) {
            for (wi::ecs::Entity joint : joint_entities) state.scene->Entity_Remove(joint);
            state.scene->Entity_Remove(entity);
            return wi::ecs::INVALID_ENTITY;
        }
        material->shaderType = wi::scene::MaterialComponent::SHADERTYPE_PBR;
        material->SetBaseColor(XMFLOAT4(material_asset.base_color[0], material_asset.base_color[1],
            material_asset.base_color[2], material_asset.base_color[3]));
        material->SetRoughness(material_asset.roughness);
        material->SetMetalness(material_asset.metallic);
        material->SetEmissiveColor(XMFLOAT4(material_asset.emissive[0], material_asset.emissive[1],
            material_asset.emissive[2], DEFAULT_SNAPSHOT_EMISSIVE_STRENGTH));
        material->SetAlphaRef(material_asset.alpha_mode == ELISA_RENDER_SCENE_ALPHA_MASK
            ? material_asset.alpha_cutoff : OPAQUE_SNAPSHOT_ALPHA_REF);
        material->SetDoubleSided(material_asset.double_sided);
        material->userBlendMode = material_asset.alpha_mode == ELISA_RENDER_SCENE_ALPHA_BLEND
            ? wi::enums::BLENDMODE_ALPHA : wi::enums::BLENDMODE_OPAQUE;
        const bool casts_shadow = material_asset.alpha_mode != ELISA_RENDER_SCENE_ALPHA_BLEND;
        material->SetCastShadow(casts_shadow);
        object->SetCastShadow(casts_shadow);
        return entity;
    } catch (...) {
        for (wi::ecs::Entity joint : joint_entities) state.scene->Entity_Remove(joint);
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
