size_t snapshot_material_set_slot(const RenderSceneService& state, uint64_t high, uint64_t low) {
    for (size_t index = 0; index < MAX_SNAPSHOT_MATERIAL_SETS; ++index) {
        const SnapshotMaterialSet& set = state.snapshot_material_sets.sets[index];
        if (set.live && set.high == high && set.low == low) return index;
    }
    return MAX_SNAPSHOT_MATERIAL_SETS;
}

bool snapshot_material_listed_by_set(const RenderSceneService& state, uint64_t high, uint64_t low) {
    for (const SnapshotMaterialSet& set : state.snapshot_material_sets.sets) {
        if (!set.live) continue;
        for (uint32_t slot = 0; slot < set.count; ++slot) {
            if (set.material_high[slot] == high && set.material_low[slot] == low) return true;
        }
    }
    return false;
}

// True while a live instance or shared mesh draws with this material or set.
bool snapshot_material_id_in_use(const RenderSceneService& state, uint64_t high, uint64_t low) {
    for (const InstanceSlot& instance : state.instances) {
        if (instance.live && instance.material_high == high && instance.material_low == low) return true;
    }
    for (const SnapshotSharedMesh& shared : state.snapshot_shared_meshes) {
        if (shared.live && shared.material_high == high && shared.material_low == low) return true;
    }
    return false;
}

using SnapshotSlotMaterials = std::array<const SnapshotMaterialAssetSlot*, MAX_SNAPSHOT_MATERIAL_SET_SLOTS>;

// Resolves the material for each of a mesh's `slot_count` material slots. A
// material fills every slot. A set must list exactly one material per slot.
int32_t resolve_snapshot_slot_materials(const RenderSceneService& state, uint64_t high, uint64_t low,
    uint32_t slot_count, SnapshotSlotMaterials& materials) {
    materials = {};
    if (slot_count == 0 || slot_count > MAX_SNAPSHOT_MATERIAL_SET_SLOTS) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    const size_t material = snapshot_material_asset_slot(state, high, low);
    if (material < MAX_SNAPSHOT_MATERIAL_ASSETS) {
        std::fill_n(materials.begin(), slot_count, &state.snapshot_material_assets[material]);
        return ELISA_RENDER_SCENE_OK;
    }
    const size_t set_slot = snapshot_material_set_slot(state, high, low);
    if (set_slot == MAX_SNAPSHOT_MATERIAL_SETS) return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
    const SnapshotMaterialSet& set = state.snapshot_material_sets.sets[set_slot];
    if (set.count != slot_count) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    for (uint32_t slot = 0; slot < slot_count; ++slot) {
        const size_t listed = snapshot_material_asset_slot(state, set.material_high[slot], set.material_low[slot]);
        if (listed == MAX_SNAPSHOT_MATERIAL_ASSETS) return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
        materials[slot] = &state.snapshot_material_assets[listed];
    }
    return ELISA_RENDER_SCENE_OK;
}

// An object casts a shadow when any of its subsets draws with a material
// that is not alpha-blended.
bool snapshot_subsets_cast_shadow(const elisa::assets::CookedGeometry& geometry,
    const SnapshotSlotMaterials& materials) {
    for (const auto& subset : geometry.subsets) {
        const SnapshotMaterialAssetSlot* material = subset.material_slot < materials.size()
            ? materials[subset.material_slot] : nullptr;
        if (material != nullptr && material->alpha_mode != ELISA_RENDER_SCENE_ALPHA_BLEND) return true;
    }
    return false;
}
