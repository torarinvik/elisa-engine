// A set is registered in two steps because the C ABI has no arrays: stage
// each slot's material in slot order, then register the set. Staging slot 0
// starts a new set, and registration clears the staged slots.
extern "C" int32_t elisa_render_scene_v1_stage_snapshot_material_set_slot(
    uint32_t slot, uint64_t material_high, uint64_t material_low) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (state.snapshot_transaction_active) return ELISA_RENDER_SCENE_BATCH_ACTIVE;
    SnapshotMaterialSet& staged = state.snapshot_material_sets.staged;
    if (slot == 0) staged = SnapshotMaterialSet{};
    if (slot != staged.count || slot >= MAX_SNAPSHOT_MATERIAL_SET_SLOTS ||
        (material_high == 0 && material_low == 0)) {
        staged = SnapshotMaterialSet{};
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    staged.material_high[slot] = material_high;
    staged.material_low[slot] = material_low;
    ++staged.count;
    return ELISA_RENDER_SCENE_OK;
}

// Registers the staged slots as set `high:low`. Each slot must name a
// registered material, and the set ID must not be a material's. Registering
// an existing set again with the same materials is a no-op.
extern "C" int32_t elisa_render_scene_v1_register_snapshot_material_set(
    uint64_t high, uint64_t low, uint32_t count) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    SnapshotMaterialSet set = state.snapshot_material_sets.staged;
    state.snapshot_material_sets.staged = SnapshotMaterialSet{};
    if (state.snapshot_transaction_active) return ELISA_RENDER_SCENE_BATCH_ACTIVE;
    if ((high == 0 && low == 0) || count == 0 || count > MAX_SNAPSHOT_MATERIAL_SET_SLOTS ||
        count != set.count || snapshot_material_asset_slot(state, high, low) < MAX_SNAPSHOT_MATERIAL_ASSETS) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    for (uint32_t slot = 0; slot < count; ++slot) {
        if (snapshot_material_asset_slot(state, set.material_high[slot], set.material_low[slot]) ==
            MAX_SNAPSHOT_MATERIAL_ASSETS) return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
    }
    const size_t existing = snapshot_material_set_slot(state, high, low);
    if (existing < MAX_SNAPSHOT_MATERIAL_SETS) {
        const SnapshotMaterialSet& live = state.snapshot_material_sets.sets[existing];
        return live.count == set.count && live.material_high == set.material_high &&
            live.material_low == set.material_low ? ELISA_RENDER_SCENE_OK : ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    for (SnapshotMaterialSet& free_set : state.snapshot_material_sets.sets) {
        if (free_set.live) continue;
        set.high = high;
        set.low = low;
        set.live = true;
        free_set = set;
        return ELISA_RENDER_SCENE_OK;
    }
    return ELISA_RENDER_SCENE_CAPACITY;
}

// Refused while a live instance or shared mesh draws with the set.
extern "C" int32_t elisa_render_scene_v1_unregister_snapshot_material_set(uint64_t high, uint64_t low) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (state.snapshot_transaction_active) return ELISA_RENDER_SCENE_BATCH_ACTIVE;
    const size_t slot = snapshot_material_set_slot(state, high, low);
    if (slot == MAX_SNAPSHOT_MATERIAL_SETS) return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
    if (snapshot_material_id_in_use(state, high, low)) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    state.snapshot_material_sets.sets[slot] = SnapshotMaterialSet{};
    return ELISA_RENDER_SCENE_OK;
}
