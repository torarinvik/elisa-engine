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

size_t snapshot_shared_mesh_slot(const RenderSceneService& state,
    uint64_t mesh_high, uint64_t mesh_low, uint64_t material_high, uint64_t material_low) {
    for (size_t index = 0; index < MAX_SNAPSHOT_SHARED_MESHES; ++index) {
        const SnapshotSharedMesh& mesh = state.snapshot_shared_meshes[index];
        if (mesh.live && mesh.mesh_high == mesh_high && mesh.mesh_low == mesh_low &&
            mesh.material_high == material_high && mesh.material_low == material_low) return index;
    }
    return MAX_SNAPSHOT_SHARED_MESHES;
}

bool cooked_geometry_is_static(const elisa::assets::CookedGeometry& geometry) {
    return geometry.skin_indices.empty() && geometry.skin_weights.empty() &&
        geometry.skin_cluster_joints.empty() && geometry.skin_bone_names.empty() &&
        geometry.skin_joints.empty() && geometry.animation_clips.empty();
}

bool configure_shared_snapshot_mesh(wi::scene::Scene& scene, wi::ecs::Entity entity,
    const elisa::assets::CookedGeometry& geometry, wi::ecs::Entity material_entity) {
    wi::scene::MeshComponent* mesh = scene.meshes.GetComponent(entity);
    if (mesh == nullptr || material_entity == wi::ecs::INVALID_ENTITY ||
        geometry.positions.empty() || geometry.positions.size() % 3 != 0 ||
        geometry.normals.size() != geometry.positions.size() ||
        geometry.uvs.size() != geometry.positions.size() / 3 * 2 ||
        (!geometry.tangents.empty() && geometry.tangents.size() != geometry.positions.size() / 3 * 4) ||
        !cooked_geometry_is_static(geometry) || geometry.indices.empty() ||
        geometry.indices.size() % 3 != 0 ||
        geometry.indices.size() > std::numeric_limits<uint32_t>::max()) return false;

    mesh->vertex_positions.resize(geometry.positions.size() / 3);
    mesh->vertex_normals.resize(geometry.normals.size() / 3);
    mesh->vertex_tangents.resize(geometry.tangents.size() / 4);
    mesh->vertex_uvset_0.resize(geometry.uvs.size() / 2);
    for (size_t index = 0; index < mesh->vertex_positions.size(); ++index) {
        mesh->vertex_positions[index] = XMFLOAT3(geometry.positions[index * 3],
            geometry.positions[index * 3 + 1], geometry.positions[index * 3 + 2]);
        mesh->vertex_normals[index] = XMFLOAT3(geometry.normals[index * 3],
            geometry.normals[index * 3 + 1], geometry.normals[index * 3 + 2]);
        mesh->vertex_uvset_0[index] = XMFLOAT2(geometry.uvs[index * 2], geometry.uvs[index * 2 + 1]);
        if (!geometry.tangents.empty()) {
            mesh->vertex_tangents[index] = XMFLOAT4(geometry.tangents[index * 4],
                geometry.tangents[index * 4 + 1], geometry.tangents[index * 4 + 2],
                geometry.tangents[index * 4 + 3]);
        }
    }
    mesh->indices.assign(geometry.indices.begin(), geometry.indices.end());
    for (size_t triangle = 0; triangle < mesh->indices.size(); triangle += 3) {
        std::swap(mesh->indices[triangle + 1], mesh->indices[triangle + 2]);
    }
    mesh->subsets.clear();
    wi::scene::MeshComponent::MeshSubset& subset = mesh->subsets.emplace_back();
    subset.indexOffset = 0;
    subset.indexCount = uint32_t(mesh->indices.size());
    subset.materialID = material_entity;
    mesh->CreateRenderData();
    return true;
}

size_t create_or_find_shared_snapshot_mesh(RenderSceneService& state,
    const SnapshotStageRow& row, const std::string& name, int32_t& failure_status) {
    const size_t existing = snapshot_shared_mesh_slot(state,
        row.mesh_high, row.mesh_low, row.material_high, row.material_low);
    if (existing < MAX_SNAPSHOT_SHARED_MESHES) return existing;
    const size_t mesh_asset_slot_index = snapshot_mesh_asset_slot(state, row.mesh_high, row.mesh_low);
    const size_t material_asset_slot_index = snapshot_material_asset_slot(state,
        row.material_high, row.material_low);
    if (mesh_asset_slot_index == MAX_SNAPSHOT_MESH_ASSETS ||
        material_asset_slot_index == MAX_SNAPSHOT_MATERIAL_ASSETS) {
        failure_status = ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
        return MAX_SNAPSHOT_SHARED_MESHES;
    }
    const SnapshotMeshAssetSlot& mesh_asset = state.snapshot_mesh_assets[mesh_asset_slot_index];
    const SnapshotMaterialAssetSlot& material_asset = state.snapshot_material_assets[material_asset_slot_index];
    if (!cooked_geometry_is_static(mesh_asset.geometry) ||
        mesh_asset.bytes > MAX_SNAPSHOT_GEOMETRY_BYTES - state.snapshot_shared_geometry_bytes) {
        failure_status = mesh_asset.bytes > MAX_SNAPSHOT_GEOMETRY_BYTES - state.snapshot_shared_geometry_bytes
            ? ELISA_RENDER_SCENE_CAPACITY : ELISA_RENDER_SCENE_BACKEND_FAILED;
        return MAX_SNAPSHOT_SHARED_MESHES;
    }
    size_t free_slot = MAX_SNAPSHOT_SHARED_MESHES;
    for (size_t index = 0; index < MAX_SNAPSHOT_SHARED_MESHES; ++index) {
        if (!state.snapshot_shared_meshes[index].live) {
            free_slot = index;
            break;
        }
    }
    if (free_slot == MAX_SNAPSHOT_SHARED_MESHES) {
        failure_status = ELISA_RENDER_SCENE_CAPACITY;
        return free_slot;
    }

    wi::ecs::Entity entity = wi::ecs::INVALID_ENTITY;
    try {
        entity = state.scene->Entity_CreateMesh(name);
        if (entity == wi::ecs::INVALID_ENTITY ||
            !configure_shared_snapshot_mesh(*state.scene, entity, mesh_asset.geometry,
                material_asset.material_entity)) {
            if (entity != wi::ecs::INVALID_ENTITY) state.scene->Entity_Remove(entity);
            return MAX_SNAPSHOT_SHARED_MESHES;
        }
    } catch (...) {
        if (entity != wi::ecs::INVALID_ENTITY) state.scene->Entity_Remove(entity);
        return MAX_SNAPSHOT_SHARED_MESHES;
    }

    SnapshotSharedMesh& shared = state.snapshot_shared_meshes[free_slot];
    shared.mesh_high = row.mesh_high;
    shared.mesh_low = row.mesh_low;
    shared.material_high = row.material_high;
    shared.material_low = row.material_low;
    shared.entity = entity;
    shared.references = 0;
    shared.bytes = mesh_asset.bytes;
    shared.live = true;
    state.snapshot_shared_geometry_bytes += shared.bytes;
    return free_slot;
}

void release_snapshot_shared_mesh(RenderSceneService& state, size_t slot) {
    if (slot >= MAX_SNAPSHOT_SHARED_MESHES) return;
    SnapshotSharedMesh& shared = state.snapshot_shared_meshes[slot];
    if (!shared.live || shared.references == 0) return;
    --shared.references;
    if (shared.references != 0) return;
    if (shared.entity != wi::ecs::INVALID_ENTITY && state.scene != nullptr) {
        state.scene->Entity_Remove(shared.entity);
    }
    state.snapshot_shared_geometry_bytes -= shared.bytes;
    shared = SnapshotSharedMesh{};
}

void discard_unreferenced_shared_snapshot_meshes(RenderSceneService& state) {
    for (size_t index = 0; index < MAX_SNAPSHOT_SHARED_MESHES; ++index) {
        SnapshotSharedMesh& shared = state.snapshot_shared_meshes[index];
        if (!shared.live || shared.references != 0) continue;
        if (shared.entity != wi::ecs::INVALID_ENTITY && state.scene != nullptr) {
            state.scene->Entity_Remove(shared.entity);
        }
        state.snapshot_shared_geometry_bytes -= shared.bytes;
        shared = SnapshotSharedMesh{};
    }
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
    instance.shared_mesh_slot = MAX_INSTANCES;
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
        for (wi::ecs::Entity joint : row.created_joint_entities) state.scene->Entity_Remove(joint);
        row.created_joint_entities.clear();
    }
    discard_unreferenced_shared_snapshot_meshes(state);
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

wi::ecs::Entity create_snapshot_entity(RenderSceneService& state, SnapshotStageRow& row,
    size_t slot, uint64_t generation, int32_t& failure_status) {
    failure_status = ELISA_RENDER_SCENE_BACKEND_FAILED;
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
        if (cooked_geometry_is_static(mesh_asset.geometry)) {
            row.shared_mesh_slot = create_or_find_shared_snapshot_mesh(state, row, name + "_mesh", failure_status);
            if (row.shared_mesh_slot >= MAX_SNAPSHOT_SHARED_MESHES) return entity;
            entity = state.scene->Entity_CreateObject(name);
            if (entity == wi::ecs::INVALID_ENTITY) return entity;
            wi::scene::TransformComponent* transform = state.scene->transforms.GetComponent(entity);
            wi::scene::ObjectComponent* object = state.scene->objects.GetComponent(entity);
            if (transform == nullptr || object == nullptr) {
                state.scene->Entity_Remove(entity);
                return wi::ecs::INVALID_ENTITY;
            }
            apply_transform(*transform,
                row.position[0], row.position[1], row.position[2],
                row.rotation[0], row.rotation[1], row.rotation[2], row.rotation[3],
                row.scale[0], row.scale[1], row.scale[2]);
            object->meshID = state.snapshot_shared_meshes[row.shared_mesh_slot].entity;
            object->SetCastShadow(material_asset.alpha_mode != ELISA_RENDER_SCENE_ALPHA_BLEND);
            return entity;
        }
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
        row.created_joint_entities = std::move(joint_entities);
        row.created_skin_joints = mesh_asset.geometry.skin_joints;
        row.created_animation_clips = mesh_asset.geometry.animation_clips;
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
