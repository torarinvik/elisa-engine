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

size_t snapshot_texture_asset_slot(const RenderSceneService& state, uint64_t high, uint64_t low) {
    for (size_t index = 0; index < MAX_SNAPSHOT_TEXTURE_ASSETS; ++index) {
        const SnapshotTextureAssetSlot& asset = state.snapshot_texture_assets[index];
        if (asset.live && asset.high == high && asset.low == low) return index;
    }
    return MAX_SNAPSHOT_TEXTURE_ASSETS;
}

#include "render_scene_snapshot_material_set_internal.inc"

bool apply_snapshot_material_textures(RenderSceneService& state,
        const SnapshotMaterialAssetSlot& asset, wi::scene::MaterialComponent& material) {
    try {
        for (size_t slot = 0; slot < SNAPSHOT_MATERIAL_TEXTURE_SLOTS; ++slot) {
            const uint64_t high = asset.texture_high[slot];
            const uint64_t low = asset.texture_low[slot];
            if (high == 0 && low == 0) continue;
            const size_t texture_slot = snapshot_texture_asset_slot(state, high, low);
            if (texture_slot == MAX_SNAPSHOT_TEXTURE_ASSETS) return false;
            SnapshotTextureAssetSlot& texture = state.snapshot_texture_assets[texture_slot];
            const int32_t slot_code = static_cast<int32_t>(slot);
            const bool assigned = texture.section.empty()
                ? elisa::rendering::textures::assign_resolved_texture(material, slot_code, texture.canonical_path)
                : elisa::rendering::textures::assign_decoded_texture(material, slot_code,
                    texture.resources[slot], texture.resource_stem, texture.extension, texture.decoded);
            if (!assigned) return false;
        }
    } catch (...) {
        return false;
    }
    return true;
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
        !add_geometry_bytes(geometry.skin_cluster_joints.size(), sizeof(uint32_t), total) ||
        !add_geometry_bytes(geometry.subsets.size(), sizeof(elisa::assets::CookedGeometry::Subset), total)) return false;
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
    for (const auto& target : geometry.morph_targets) {
        if (!add_geometry_bytes(target.positions.size(), sizeof(float), total) ||
            !add_geometry_bytes(target.normals.size(), sizeof(float), total)) return false;
    }
    if (!add_geometry_bytes(geometry.cameras.size(), sizeof(elisa::assets::CookedGeometry::Camera), total) ||
        !add_geometry_bytes(geometry.lights.size(), sizeof(elisa::assets::CookedGeometry::Light), total)) return false;
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
        geometry.skin_joints.empty() && geometry.animation_clips.empty() && geometry.morph_targets.empty();
}

bool configure_shared_snapshot_mesh(wi::scene::Scene& scene, wi::ecs::Entity entity,
    const elisa::assets::CookedGeometry& geometry, const SnapshotSlotMaterials& materials) {
    wi::scene::MeshComponent* mesh = scene.meshes.GetComponent(entity);
    if (mesh == nullptr || geometry.subsets.empty() || geometry.positions.empty() || geometry.positions.size() % 3 != 0 ||
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
        mesh->vertex_positions[index] = elisa::rendering::cooked_vector(geometry.positions, index);
        mesh->vertex_normals[index] = elisa::rendering::cooked_vector(geometry.normals, index);
        mesh->vertex_uvset_0[index] = XMFLOAT2(geometry.uvs[index * 2], geometry.uvs[index * 2 + 1]);
        if (!geometry.tangents.empty()) {
            mesh->vertex_tangents[index] = elisa::rendering::cooked_tangent(geometry.tangents, index);
        }
    }
    elisa::rendering::assign_cooked_indices(*mesh, geometry.indices);
    // One Wicked subset per cooked subset, drawn with its slot's material.
    mesh->subsets.clear();
    for (const auto& cooked : geometry.subsets) {
        const SnapshotMaterialAssetSlot* material = cooked.material_slot < materials.size()
            ? materials[cooked.material_slot] : nullptr;
        if (material == nullptr || material->material_entity == wi::ecs::INVALID_ENTITY ||
            cooked.index_count > mesh->indices.size() - std::min<size_t>(cooked.index_start, mesh->indices.size())) {
            return false;
        }
        wi::scene::MeshComponent::MeshSubset& subset = mesh->subsets.emplace_back();
        subset.indexOffset = cooked.index_start;
        subset.indexCount = cooked.index_count;
        subset.materialID = material->material_entity;
    }
    mesh->CreateRenderData();
    return true;
}

size_t create_or_find_shared_snapshot_mesh(RenderSceneService& state, const SnapshotStageRow& row,
    const SnapshotSlotMaterials& materials, const std::string& name, int32_t& failure_status) {
    const size_t existing = snapshot_shared_mesh_slot(state,
        row.mesh_high, row.mesh_low, row.material_high, row.material_low);
    if (existing < MAX_SNAPSHOT_SHARED_MESHES) return existing;
    const size_t mesh_asset_slot_index = snapshot_mesh_asset_slot(state, row.mesh_high, row.mesh_low);
    if (mesh_asset_slot_index == MAX_SNAPSHOT_MESH_ASSETS) {
        failure_status = ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
        return MAX_SNAPSHOT_SHARED_MESHES;
    }
    const SnapshotMeshAssetSlot& mesh_asset = state.snapshot_mesh_assets[mesh_asset_slot_index];
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
            !configure_shared_snapshot_mesh(*state.scene, entity, mesh_asset.geometry, materials)) {
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
    instance.shared_mesh_slot = NO_SHARED_MESH;
    instance.joint_entities.clear();
    instance.skin_joints.clear();
    instance.animation_clips.clear();
    instance.animation_submission = {};
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

// Commit only moves a retained instance and sets its tint, so its row must
// name the instance's own identity, mesh, and material or material set. Any
// other change retires the instance and stages a new row.
bool snapshot_row_retains_instance(const SnapshotStageRow& row, const InstanceSlot& instance) {
    const bool same_identity = row.gameplay_epoch == instance.gameplay_epoch &&
        row.gameplay_id == instance.gameplay_id && row.render_id == instance.render_id;
    const bool same_mesh = row.mesh_high == instance.mesh_high && row.mesh_low == instance.mesh_low;
    const bool same_material = row.material_high == instance.material_high &&
        row.material_low == instance.material_low;
    return same_identity && same_mesh && same_material;
}

bool snapshot_tint_color_valid(float red, float green, float blue) {
    const auto channel = [](float value) {
        return std::isfinite(value) && value >= 0.0f && value <= MAX_SNAPSHOT_TINT_COLOR_SCALE;
    };
    return channel(red) && channel(green) && channel(blue);
}

wi::ecs::Entity create_snapshot_entity(RenderSceneService& state, SnapshotStageRow& row,
    size_t slot, uint64_t generation, int32_t& failure_status) {
    failure_status = ELISA_RENDER_SCENE_BACKEND_FAILED;
    wi::ecs::Entity entity = wi::ecs::INVALID_ENTITY;
    std::vector<wi::ecs::Entity> joint_entities;
    try {
        const size_t mesh_slot = snapshot_mesh_asset_slot(state, row.mesh_high, row.mesh_low);
        if (mesh_slot == MAX_SNAPSHOT_MESH_ASSETS) return entity;
        const SnapshotMeshAssetSlot& mesh_asset = state.snapshot_mesh_assets[mesh_slot];
        SnapshotSlotMaterials materials;
        const int32_t resolved = resolve_snapshot_slot_materials(state, row.material_high, row.material_low,
            mesh_asset.geometry.material_slots, materials);
        if (resolved != ELISA_RENDER_SCENE_OK) {
            failure_status = resolved;
            return entity;
        }
        const SnapshotMaterialAssetSlot& material_asset = *materials[0];
        const std::string name = "elisa_snapshot_" + std::to_string(row.render_id) + "_" +
            std::to_string(slot) + "_" + std::to_string(generation);
        if (cooked_geometry_is_static(mesh_asset.geometry)) {
            row.shared_mesh_slot = create_or_find_shared_snapshot_mesh(state, row, materials,
                name + "_mesh", failure_status);
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
            object->SetCastShadow(snapshot_subsets_cast_shadow(mesh_asset.geometry, materials));
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
        wi::scene::MeshComponent* skinned_mesh = state.scene->meshes.GetComponent(entity);
        if (skinned_mesh == nullptr || skinned_mesh->subsets.size() != mesh_asset.geometry.subsets.size()) {
            for (wi::ecs::Entity joint : joint_entities) state.scene->Entity_Remove(joint);
            state.scene->Entity_Remove(entity);
            return wi::ecs::INVALID_ENTITY;
        }
        for (size_t subset = 0; subset < skinned_mesh->subsets.size(); ++subset) {
            const uint32_t material_slot = mesh_asset.geometry.subsets[subset].material_slot;
            if (material_slot >= materials.size() || materials[material_slot] == nullptr ||
                materials[material_slot]->material_entity == wi::ecs::INVALID_ENTITY) {
                for (wi::ecs::Entity joint : joint_entities) state.scene->Entity_Remove(joint);
                state.scene->Entity_Remove(entity);
                return wi::ecs::INVALID_ENTITY;
            }
            skinned_mesh->subsets[subset].materialID = materials[material_slot]->material_entity;
        }
        skinned_mesh->CreateRenderData();
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
        if (!apply_snapshot_material_textures(state, material_asset, *material)) {
            for (wi::ecs::Entity joint : joint_entities) state.scene->Entity_Remove(joint);
            state.scene->Entity_Remove(entity);
            failure_status = ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
            return wi::ecs::INVALID_ENTITY;
        }
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

#include "render_scene_snapshot_asset_internal.inc"
