// Snapshot material registration shared by the material ABI and the cooked
// mesh-material ABI. Callers hold the service mutex on the owner thread,
// outside a snapshot transaction.

// Occlusion reads either the packed surface texture or a dedicated texture.
bool snapshot_material_values_valid(const SnapshotMaterialAssetSlot& values) {
    const auto unit = [](float value) { return finite(value) && value >= 0.0f && value <= 1.0f; };
    const auto emission = [](float value) { return bounded(value, 1.0e4f) && value >= 0.0f; };
    const size_t surface = static_cast<size_t>(elisa::rendering::textures::Slot::Surface);
    const size_t occlusion = static_cast<size_t>(elisa::rendering::textures::Slot::Occlusion);
    const bool surfaced = values.texture_high[surface] != 0 || values.texture_low[surface] != 0;
    const bool separately_occluded = values.texture_high[occlusion] != 0 || values.texture_low[occlusion] != 0;
    return valid_color(values.base_color[0], values.base_color[1], values.base_color[2], values.base_color[3]) &&
        unit(values.metallic) && unit(values.roughness) &&
        finite(values.normal_scale) &&
        std::abs(values.normal_scale) <= elisa::assets::WICKED_NORMAL_SCALE_LIMIT &&
        std::all_of(std::begin(values.emissive), std::end(values.emissive), emission) &&
        unit(values.alpha_cutoff) && values.alpha_mode >= ELISA_RENDER_SCENE_ALPHA_OPAQUE &&
        values.alpha_mode <= ELISA_RENDER_SCENE_ALPHA_BLEND && (!values.occlusion || surfaced || separately_occluded);
}

bool snapshot_material_values_match(const SnapshotMaterialAssetSlot& asset,
        const SnapshotMaterialAssetSlot& values) {
    return std::equal(std::begin(asset.base_color), std::end(asset.base_color), std::begin(values.base_color)) &&
        asset.metallic == values.metallic && asset.roughness == values.roughness &&
        asset.normal_scale == values.normal_scale &&
        std::equal(std::begin(asset.emissive), std::end(asset.emissive), std::begin(values.emissive)) &&
        asset.alpha_cutoff == values.alpha_cutoff && asset.alpha_mode == values.alpha_mode &&
        asset.double_sided == values.double_sided && asset.occlusion == values.occlusion &&
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
        material->SetNormalMapStrength(values.normal_scale);
        material->SetEmissiveColor(XMFLOAT4(values.emissive[0], values.emissive[1],
            values.emissive[2], DEFAULT_SNAPSHOT_EMISSIVE_STRENGTH));
        material->SetAlphaRef(values.alpha_mode == ELISA_RENDER_SCENE_ALPHA_MASK
            ? values.alpha_cutoff : OPAQUE_SNAPSHOT_ALPHA_REF);
        material->SetDoubleSided(values.double_sided);
        material->SetOcclusionEnabled_Primary(values.occlusion);
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

// The texture registered from image `section` of the bundle at
// `canonical_path`: missing is a load failure, and two IDs are ambiguous.
int32_t bundle_image_texture(const RenderSceneService& state, const std::filesystem::path& canonical_path,
        const std::string& section, size_t& found) {
    found = MAX_SNAPSHOT_TEXTURE_ASSETS;
    for (size_t index = 0; index < MAX_SNAPSHOT_TEXTURE_ASSETS; ++index) {
        const SnapshotTextureAssetSlot& texture = state.snapshot_texture_assets[index];
        if (!texture.live || texture.section != section || texture.canonical_path != canonical_path) continue;
        if (found != MAX_SNAPSHOT_TEXTURE_ASSETS) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
        found = index;
    }
    return found == MAX_SNAPSHOT_TEXTURE_ASSETS ? ELISA_RENDER_SCENE_ASSET_LOAD_FAILED : ELISA_RENDER_SCENE_OK;
}

// The registration values for cooked material `slot` of `mesh`. Each image
// the slot samples must be registered from the mesh's own bundle under
// exactly one texture ID.
int32_t cooked_slot_material_values(const RenderSceneService& state, const SnapshotMeshAssetSlot& mesh,
        uint32_t slot, SnapshotMaterialAssetSlot& values) {
    const elisa::assets::CookedGeometry::SlotMaterial& cooked = mesh.geometry.slot_materials[slot];
    values = SnapshotMaterialAssetSlot{};
    std::copy(cooked.base_color.begin(), cooked.base_color.end(), std::begin(values.base_color));
    values.metallic = cooked.metallic;
    values.roughness = cooked.roughness;
    std::copy(cooked.emissive.begin(), cooked.emissive.end(), std::begin(values.emissive));
    values.alpha_cutoff = cooked.alpha_cutoff;
    values.normal_scale = cooked.normal_scale;
    values.alpha_mode = int32_t(cooked.alpha_mode);
    values.double_sided = cooked.double_sided;
    values.occlusion = cooked.occlusion;
    for (size_t texture_slot = 0; texture_slot < SNAPSHOT_MATERIAL_TEXTURE_SLOTS; ++texture_slot) {
        const uint32_t reference = cooked.textures[texture_slot];
        if (reference == 0) continue;
        // A glTF occlusion map that aliases the packed surface image is
        // already represented by SURFACEMAP. Keep the runtime registration
        // canonical so hand-registered materials with the same surface map
        // compare equal and do not upload the same resource twice.
        if (texture_slot == static_cast<size_t>(elisa::rendering::textures::Slot::Occlusion) &&
            reference == cooked.textures[static_cast<size_t>(elisa::rendering::textures::Slot::Surface)]) continue;
        size_t found = MAX_SNAPSHOT_TEXTURE_ASSETS;
        const int32_t status = bundle_image_texture(state, mesh.canonical_path,
            mesh.geometry.texture_sections[reference - 1], found);
        if (status != ELISA_RENDER_SCENE_OK) return status;
        values.texture_high[texture_slot] = state.snapshot_texture_assets[found].high;
        values.texture_low[texture_slot] = state.snapshot_texture_assets[found].low;
    }
    return ELISA_RENDER_SCENE_OK;
}

// Resident mesh `high:low`, whose cooked slot materials and images the
// mesh-material calls read, or why there is none: a zero ID is invalid, and
// a missing mesh is pending or failed.
int32_t cooked_mesh_asset(const RenderSceneService& state, uint64_t high, uint64_t low,
        const SnapshotMeshAssetSlot*& mesh) {
    mesh = nullptr;
    if (high == 0 && low == 0) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    const size_t slot = snapshot_mesh_asset_slot(state, high, low);
    if (slot == MAX_SNAPSHOT_MESH_ASSETS) {
        return snapshot_missing_asset_status(state, SnapshotAssetKind::Mesh, high, low);
    }
    mesh = &state.snapshot_mesh_assets[slot];
    return ELISA_RENDER_SCENE_OK;
}
