// A cooked mesh can carry the glTF factors authored for each material slot.
// These calls register them as ordinary snapshot materials, so a game names
// a slot's material instead of restating its factors.

// The number of slot materials cooked into resident mesh `high:low`: 0 when
// its source authored none, otherwise its material slot count.
extern "C" int32_t elisa_render_scene_v1_snapshot_mesh_material_count(uint64_t mesh_high, uint64_t mesh_low) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const std::vector<elisa::assets::CookedGeometry::SlotMaterial>* cooked = nullptr;
    const int32_t status = cooked_slot_materials(state, mesh_high, mesh_low, cooked);
    return status == ELISA_RENDER_SCENE_OK ? int32_t(cooked->size()) : status;
}

// Registers the cooked material of one slot as material `high:low`, with
// the same no-op and conflict rules as any material registration.
extern "C" int32_t elisa_render_scene_v1_register_snapshot_mesh_material(
    uint64_t mesh_high, uint64_t mesh_low, uint32_t slot, uint64_t high, uint64_t low) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (state.snapshot_transaction_active) return ELISA_RENDER_SCENE_BATCH_ACTIVE;
    const std::vector<elisa::assets::CookedGeometry::SlotMaterial>* cooked = nullptr;
    const int32_t status = cooked_slot_materials(state, mesh_high, mesh_low, cooked);
    if (status != ELISA_RENDER_SCENE_OK) return status;
    if (slot >= cooked->size()) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    bool created = false;
    return register_snapshot_material_locked(state, high, low, cooked_slot_material_values((*cooked)[slot]), created);
}

// Registers every cooked slot material of a mesh under the material IDs
// staged with elisa_render_scene_v1_stage_snapshot_material_set_slot, in slot
// order, then registers those IDs as set `set_high:set_low`. `count` must
// match both the staging and the mesh's cooked slots. It consumes the
// staging, and on failure removes every material it created.
extern "C" int32_t elisa_render_scene_v1_register_snapshot_mesh_material_set(
    uint64_t mesh_high, uint64_t mesh_low, uint64_t set_high, uint64_t set_low, uint32_t count) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const SnapshotMaterialSet set = state.snapshot_material_sets.staged;
    state.snapshot_material_sets.staged = SnapshotMaterialSet{};
    if (state.snapshot_transaction_active) return ELISA_RENDER_SCENE_BATCH_ACTIVE;
    const std::vector<elisa::assets::CookedGeometry::SlotMaterial>* cooked = nullptr;
    const int32_t status = cooked_slot_materials(state, mesh_high, mesh_low, cooked);
    if (status != ELISA_RENDER_SCENE_OK) return status;
    if (count == 0 || count != set.count || count != cooked->size()) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    std::array<bool, MAX_SNAPSHOT_MATERIAL_SET_SLOTS> created{};
    const auto roll_back = [&state, &set, &created](int32_t failure) {
        for (uint32_t slot = 0; slot < set.count; ++slot) {
            if (!created[slot]) continue;
            remove_snapshot_material(state,
                snapshot_material_asset_slot(state, set.material_high[slot], set.material_low[slot]));
        }
        return failure;
    };
    for (uint32_t slot = 0; slot < count; ++slot) {
        const int32_t registered = register_snapshot_material_locked(state, set.material_high[slot],
            set.material_low[slot], cooked_slot_material_values((*cooked)[slot]), created[slot]);
        if (registered != ELISA_RENDER_SCENE_OK) return roll_back(registered);
    }
    const int32_t registered = register_snapshot_material_set_locked(state, set_high, set_low, set, count);
    return registered == ELISA_RENDER_SCENE_OK ? registered : roll_back(registered);
}
