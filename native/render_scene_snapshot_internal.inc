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
                ? elisa::rendering::textures::assign_resolved_texture(material, slot_code,
                    texture.canonical_path, &texture.resources[slot])
                : texture.format == elisa::assets::EncodedImageFormat::Ktx2
                    ? elisa::rendering::textures::assign_encoded_ktx2_texture(material, slot_code,
                        texture.resources[slot], texture.encoded_ktx2)
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
        !add_geometry_bytes(geometry.skin_inverse_bind_matrices.size(), sizeof(float), total) ||
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
    uint64_t mesh_high, uint64_t mesh_low, uint64_t material_high, uint64_t material_low,
    uint32_t placement_index) {
    for (size_t index = 0; index < MAX_SNAPSHOT_SHARED_MESHES; ++index) {
        const SnapshotSharedMesh& mesh = state.snapshot_shared_meshes[index];
        if (mesh.live && mesh.mesh_high == mesh_high && mesh.mesh_low == mesh_low &&
            mesh.material_high == material_high && mesh.material_low == material_low &&
            mesh.placement_index == placement_index) return index;
    }
    return MAX_SNAPSHOT_SHARED_MESHES;
}

bool cooked_geometry_is_static(const elisa::assets::CookedGeometry& geometry) {
    return geometry.skin_indices.empty() && geometry.skin_weights.empty() &&
        geometry.skin_inverse_bind_matrices.empty() && geometry.skin_cluster_joints.empty() &&
        geometry.skin_bone_names.empty() &&
        geometry.skin_joints.empty() && geometry.animation_clips.empty() && geometry.morph_targets.empty();
}

bool configure_shared_snapshot_mesh(wi::scene::Scene& scene, wi::ecs::Entity entity,
    const elisa::assets::CookedGeometry& geometry, const SnapshotSlotMaterials& materials,
    uint32_t placement_index) {
    wi::scene::MeshComponent* mesh = scene.meshes.GetComponent(entity);
    if (mesh == nullptr || geometry.subsets.empty() || geometry.positions.empty() || geometry.positions.size() % 3 != 0 ||
        geometry.normals.size() != geometry.positions.size() ||
        geometry.uvs.size() != geometry.positions.size() / 3 * 2 ||
        (!geometry.tangents.empty() && geometry.tangents.size() != geometry.positions.size() / 3 * 4) ||
        !cooked_geometry_is_static(geometry) || geometry.indices.empty() || geometry.indices.size() % 3 != 0 ||
        geometry.indices.size() > std::numeric_limits<uint32_t>::max()) return false;

    size_t vertex_start = 0;
    size_t vertex_count = geometry.positions.size() / 3;
    size_t index_start = 0;
    size_t index_count = geometry.indices.size();
    size_t subset_start = 0;
    size_t subset_count = geometry.subsets.size();
    if (!geometry.mesh_placements.empty()) {
        if (placement_index >= geometry.mesh_placements.size()) return false;
        const auto& placement = geometry.mesh_placements[placement_index];
        vertex_start = placement.vertex_start;
        vertex_count = placement.vertex_count;
        index_start = placement.index_start;
        index_count = placement.index_count;
        subset_start = placement.subset_start;
        subset_count = placement.subset_count;
    } else if (placement_index != 0) {
        return false;
    }
    if (vertex_count == 0 || index_count == 0 || index_count % 3 != 0 || subset_count == 0 ||
        vertex_start + vertex_count > geometry.positions.size() / 3 ||
        index_start + index_count > geometry.indices.size() || subset_start + subset_count > geometry.subsets.size()) {
        return false;
    }

    mesh->vertex_positions.resize(vertex_count);
    mesh->vertex_normals.resize(vertex_count);
    mesh->vertex_tangents.resize(geometry.tangents.empty() ? 0 : vertex_count);
    mesh->vertex_uvset_0.resize(vertex_count);
    for (size_t index = 0; index < mesh->vertex_positions.size(); ++index) {
        const size_t source = vertex_start + index;
        mesh->vertex_positions[index] = elisa::rendering::cooked_vector(geometry.positions, source);
        mesh->vertex_normals[index] = elisa::rendering::cooked_vector(geometry.normals, source);
        mesh->vertex_uvset_0[index] = XMFLOAT2(geometry.uvs[source * 2], geometry.uvs[source * 2 + 1]);
        if (!geometry.tangents.empty()) {
            mesh->vertex_tangents[index] = elisa::rendering::cooked_tangent(geometry.tangents, source);
        }
    }
    std::vector<uint32_t> local_indices;
    local_indices.reserve(index_count);
    for (size_t offset = 0; offset < index_count; ++offset) {
        const uint32_t source = geometry.indices[index_start + offset];
        if (source < vertex_start || size_t(source) >= vertex_start + vertex_count) return false;
        local_indices.push_back(uint32_t(size_t(source) - vertex_start));
    }
    elisa::rendering::assign_cooked_indices(*mesh, local_indices);
    mesh->subsets.clear();
    size_t subset_coverage = 0;
    for (size_t subset_index = subset_start; subset_index < subset_start + subset_count; ++subset_index) {
        const auto& cooked = geometry.subsets[subset_index];
        const size_t cooked_end = size_t(cooked.index_start) + cooked.index_count;
        const size_t begin = std::max<size_t>(cooked.index_start, index_start);
        const size_t end = std::min(cooked_end, index_start + index_count);
        if (begin >= end) continue;
        if ((begin - index_start) % 3 != 0 || (end - begin) % 3 != 0) return false;
        const SnapshotMaterialAssetSlot* material = cooked.material_slot < materials.size()
            ? materials[cooked.material_slot] : nullptr;
        if (material == nullptr || material->material_entity == wi::ecs::INVALID_ENTITY ||
            end > index_start + index_count) return false;
        wi::scene::MeshComponent::MeshSubset& subset = mesh->subsets.emplace_back();
        subset.indexOffset = uint32_t(begin - index_start);
        subset.indexCount = uint32_t(end - begin);
        subset.materialID = material->material_entity;
        subset_coverage += end - begin;
    }
    if (mesh->subsets.empty() || subset_coverage != index_count) return false;
    mesh->CreateRenderData();
    return true;
}

size_t create_or_find_shared_snapshot_mesh(RenderSceneService& state, const SnapshotStageRow& row,
    const SnapshotSlotMaterials& materials, const std::string& name, uint32_t placement_index,
    int32_t& failure_status) {
    const size_t existing = snapshot_shared_mesh_slot(state,
        row.mesh_high, row.mesh_low, row.material_high, row.material_low, placement_index);
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
            !configure_shared_snapshot_mesh(*state.scene, entity, mesh_asset.geometry, materials, placement_index)) {
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
    shared.placement_index = placement_index;
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

void clear_snapshot_instance(RenderSceneService& state, InstanceSlot& instance) {
    clear_instance_pick_bindings(state, instance);
    instance.entity = wi::ecs::INVALID_ENTITY;
    instance.gameplay_epoch = 0;
    instance.gameplay_id = 0;
    instance.render_id = 0;
    instance.mesh_high = 0;
    instance.mesh_low = 0;
    instance.material_high = 0;
    instance.material_low = 0;
    instance.shared_mesh_slot = NO_SHARED_MESH;
    instance.snapshot_additional_shared_mesh_slots.clear();
    instance.shared_source = wi::ecs::INVALID_ENTITY;
    instance.joint_entities.clear();
    instance.imported_mesh_entities.clear();
    instance.snapshot_placement_entities.clear();
    instance.imported_camera_entities.clear();
    instance.imported_light_handles.clear();
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
        for (wi::ecs::Entity placement : row.created_placement_entities) state.scene->Entity_Remove(placement);
        row.created_placement_entities.clear();
        row.created_additional_shared_mesh_slots.clear();
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

#include "render_scene_snapshot_draw_internal.inc"

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
                name + "_mesh_0", 0, failure_status);
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
            const size_t placement_count = mesh_asset.geometry.mesh_placements.empty()
                ? 1 : mesh_asset.geometry.mesh_placements.size();
            row.created_placement_entities.reserve(placement_count - 1);
            row.created_additional_shared_mesh_slots.reserve(placement_count - 1);
            for (size_t placement = 1; placement < placement_count; ++placement) {
                const size_t shared_slot = create_or_find_shared_snapshot_mesh(state, row, materials,
                    name + "_mesh_" + std::to_string(placement), uint32_t(placement), failure_status);
                if (shared_slot >= MAX_SNAPSHOT_SHARED_MESHES) {
                    state.scene->Entity_Remove(entity);
                    return wi::ecs::INVALID_ENTITY;
                }
                const wi::ecs::Entity child = state.scene->Entity_CreateObject(
                    name + "_placement_" + std::to_string(placement));
                if (child == wi::ecs::INVALID_ENTITY) {
                    state.scene->Entity_Remove(entity);
                    return wi::ecs::INVALID_ENTITY;
                }
                wi::scene::TransformComponent* child_transform = state.scene->transforms.GetComponent(child);
                wi::scene::ObjectComponent* child_object = state.scene->objects.GetComponent(child);
                if (child_transform == nullptr || child_object == nullptr) {
                    if (child != wi::ecs::INVALID_ENTITY) state.scene->Entity_Remove(child);
                    state.scene->Entity_Remove(entity);
                    return wi::ecs::INVALID_ENTITY;
                }
                state.scene->Component_Attach(child, entity, true);
                const wi::scene::TransformComponent* parent_transform = state.scene->transforms.GetComponent(entity);
                if (parent_transform == nullptr) {
                    state.scene->Entity_Remove(child);
                    state.scene->Entity_Remove(entity);
                    return wi::ecs::INVALID_ENTITY;
                }
                child_transform->UpdateTransform_Parented(*parent_transform);
                child_object->meshID = state.snapshot_shared_meshes[shared_slot].entity;
                child_object->SetCastShadow(snapshot_subsets_cast_shadow(mesh_asset.geometry, materials));
                row.created_additional_shared_mesh_slots.push_back(shared_slot);
                row.created_placement_entities.push_back(child);
            }
            return entity;
        }
        return create_animated_snapshot_entity(state, row, mesh_asset, materials,
            material_asset, name, failure_status, joint_entities);
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
