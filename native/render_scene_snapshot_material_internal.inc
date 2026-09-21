// Snapshot material registration shared by the material ABI and the cooked
// mesh-material ABI. Callers hold the service mutex on the owner thread,
// outside a snapshot transaction.

bool snapshot_material_values_valid(const SnapshotMaterialAssetSlot& values) {
    const auto unit = [](float value) { return finite(value) && value >= 0.0f && value <= 1.0f; };
    const auto emission = [](float value) { return bounded(value, 1.0e4f) && value >= 0.0f; };
    return valid_color(values.base_color[0], values.base_color[1], values.base_color[2], values.base_color[3]) &&
        unit(values.metallic) && unit(values.roughness) &&
        std::all_of(std::begin(values.emissive), std::end(values.emissive), emission) &&
        unit(values.alpha_cutoff) && values.alpha_mode >= ELISA_RENDER_SCENE_ALPHA_OPAQUE &&
        values.alpha_mode <= ELISA_RENDER_SCENE_ALPHA_BLEND;
}

bool snapshot_material_values_match(const SnapshotMaterialAssetSlot& asset,
        const SnapshotMaterialAssetSlot& values) {
    return std::equal(std::begin(asset.base_color), std::end(asset.base_color), std::begin(values.base_color)) &&
        asset.metallic == values.metallic && asset.roughness == values.roughness &&
        std::equal(std::begin(asset.emissive), std::end(asset.emissive), std::begin(values.emissive)) &&
        asset.alpha_cutoff == values.alpha_cutoff && asset.alpha_mode == values.alpha_mode &&
        asset.double_sided == values.double_sided &&
        asset.texture_high == values.texture_high && asset.texture_low == values.texture_low;
}

// Registers material `high:low` with the factors and textures in `values`,
// or confirms an identical registration. `created` reports whether this call
// made a new material.
int32_t register_snapshot_material_locked(RenderSceneService& state, uint64_t high, uint64_t low,
        const SnapshotMaterialAssetSlot& values, bool& created) {
    created = false;
    if ((high == 0 && low == 0) || !snapshot_material_values_valid(values) ||
        snapshot_material_set_slot(state, high, low) < MAX_SNAPSHOT_MATERIAL_SETS) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    for (size_t texture_slot = 0; texture_slot < SNAPSHOT_MATERIAL_TEXTURE_SLOTS; ++texture_slot) {
        const uint64_t texture_high = values.texture_high[texture_slot];
        const uint64_t texture_low = values.texture_low[texture_slot];
        if ((texture_high != 0 || texture_low != 0) &&
            snapshot_texture_asset_slot(state, texture_high, texture_low) == MAX_SNAPSHOT_TEXTURE_ASSETS) {
            return snapshot_missing_asset_status(state, SnapshotAssetKind::Texture, texture_high, texture_low);
        }
    }
    const size_t existing = snapshot_material_asset_slot(state, high, low);
    if (existing < MAX_SNAPSHOT_MATERIAL_ASSETS) {
        return snapshot_material_values_match(state.snapshot_material_assets[existing], values)
            ? ELISA_RENDER_SCENE_OK : ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    size_t free_slot = MAX_SNAPSHOT_MATERIAL_ASSETS;
    for (size_t index = 0; index < MAX_SNAPSHOT_MATERIAL_ASSETS; ++index) {
        if (!state.snapshot_material_assets[index].live) {
            free_slot = index;
            break;
        }
    }
    if (free_slot == MAX_SNAPSHOT_MATERIAL_ASSETS) return ELISA_RENDER_SCENE_CAPACITY;
    wi::ecs::Entity material_entity = wi::ecs::INVALID_ENTITY;
    const std::string name = "elisa_snapshot_material_" + std::to_string(high) + "_" + std::to_string(low);
    try {
        material_entity = state.scene->Entity_CreateMaterial(name);
        wi::scene::MaterialComponent* material = state.scene->materials.GetComponent(material_entity);
        if (material_entity == wi::ecs::INVALID_ENTITY || material == nullptr) {
            if (material_entity != wi::ecs::INVALID_ENTITY) state.scene->Entity_Remove(material_entity);
            return ELISA_RENDER_SCENE_BACKEND_FAILED;
        }
        material->shaderType = wi::scene::MaterialComponent::SHADERTYPE_PBR;
        material->SetBaseColor(XMFLOAT4(values.base_color[0], values.base_color[1],
            values.base_color[2], values.base_color[3]));
        material->SetRoughness(values.roughness);
        material->SetMetalness(values.metallic);
        material->SetEmissiveColor(XMFLOAT4(values.emissive[0], values.emissive[1],
            values.emissive[2], DEFAULT_SNAPSHOT_EMISSIVE_STRENGTH));
        material->SetAlphaRef(values.alpha_mode == ELISA_RENDER_SCENE_ALPHA_MASK
            ? values.alpha_cutoff : OPAQUE_SNAPSHOT_ALPHA_REF);
        material->SetDoubleSided(values.double_sided);
        material->userBlendMode = values.alpha_mode == ELISA_RENDER_SCENE_ALPHA_BLEND
            ? wi::enums::BLENDMODE_ALPHA : wi::enums::BLENDMODE_OPAQUE;
        material->SetCastShadow(values.alpha_mode != ELISA_RENDER_SCENE_ALPHA_BLEND);
        if (!apply_snapshot_material_textures(state, values, *material)) {
            state.scene->Entity_Remove(material_entity);
            return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
        }
        material->CreateRenderData();
    } catch (...) {
        if (material_entity != wi::ecs::INVALID_ENTITY) state.scene->Entity_Remove(material_entity);
        return ELISA_RENDER_SCENE_BACKEND_FAILED;
    }
    SnapshotMaterialAssetSlot& asset = state.snapshot_material_assets[free_slot];
    asset = values;
    asset.high = high;
    asset.low = low;
    asset.material_entity = material_entity;
    asset.live = true;
    created = true;
    return ELISA_RENDER_SCENE_OK;
}

void remove_snapshot_material(RenderSceneService& state, size_t slot) {
    const wi::ecs::Entity material_entity = state.snapshot_material_assets[slot].material_entity;
    if (material_entity != wi::ecs::INVALID_ENTITY) state.scene->Entity_Remove(material_entity);
    state.snapshot_material_assets[slot] = SnapshotMaterialAssetSlot{};
}

SnapshotMaterialAssetSlot cooked_slot_material_values(const elisa::assets::CookedGeometry::SlotMaterial& cooked) {
    SnapshotMaterialAssetSlot values;
    std::copy(cooked.base_color.begin(), cooked.base_color.end(), std::begin(values.base_color));
    values.metallic = cooked.metallic;
    values.roughness = cooked.roughness;
    std::copy(cooked.emissive.begin(), cooked.emissive.end(), std::begin(values.emissive));
    values.alpha_cutoff = cooked.alpha_cutoff;
    values.alpha_mode = int32_t(cooked.alpha_mode);
    values.double_sided = cooked.double_sided;
    return values;
}

// The slot materials cooked into resident mesh `high:low`, or why there are
// none to read: a zero ID is invalid, and a missing mesh is pending or failed.
int32_t cooked_slot_materials(const RenderSceneService& state, uint64_t high, uint64_t low,
        const std::vector<elisa::assets::CookedGeometry::SlotMaterial>*& materials) {
    materials = nullptr;
    if (high == 0 && low == 0) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    const size_t mesh = snapshot_mesh_asset_slot(state, high, low);
    if (mesh == MAX_SNAPSHOT_MESH_ASSETS) {
        return snapshot_missing_asset_status(state, SnapshotAssetKind::Mesh, high, low);
    }
    materials = &state.snapshot_mesh_assets[mesh].geometry.slot_materials;
    return ELISA_RENDER_SCENE_OK;
}
