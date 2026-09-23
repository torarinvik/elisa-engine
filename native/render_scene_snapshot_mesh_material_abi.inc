// A cooked mesh can carry the glTF factors and bundle images authored for
// each material slot. These calls register them as ordinary snapshot
// textures and materials, so a game names a slot's material instead of
// restating it.

// The number of slot materials cooked into resident mesh `high:low`: 0 when
// its source authored none, otherwise its material slot count.
extern "C" int32_t elisa_render_scene_v1_snapshot_mesh_material_count(uint64_t mesh_high, uint64_t mesh_low) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const SnapshotMeshAssetSlot* mesh = nullptr;
    const int32_t status = cooked_mesh_asset(state, mesh_high, mesh_low, mesh);
    return status == ELISA_RENDER_SCENE_OK ? int32_t(mesh->geometry.slot_materials.size()) : status;
}

// Copies one cooked material-slot name as UTF-8 bytes without a terminator.
// Names are optional in cooked packages; an unnamed slot succeeds with length 0.
extern "C" int32_t elisa_render_scene_v1_snapshot_mesh_material_name(
    uint64_t mesh_high, uint64_t mesh_low, uint32_t slot,
    uint8_t* name_bytes, uint32_t name_capacity, uint32_t* name_length) {
    if (name_bytes == nullptr || name_length == nullptr) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const SnapshotMeshAssetSlot* mesh = nullptr;
    const int32_t status = cooked_mesh_asset(state, mesh_high, mesh_low, mesh);
    if (status != ELISA_RENDER_SCENE_OK) return status;
    if (slot >= mesh->geometry.slot_materials.size()) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    static const std::string empty_name;
    const std::string& name = mesh->geometry.slot_material_names.empty()
        ? empty_name : mesh->geometry.slot_material_names[slot];
    *name_length = uint32_t(name.size());
    if (name.size() > name_capacity) return ELISA_RENDER_SCENE_CAPACITY;
    if (!name.empty()) std::memcpy(name_bytes, name.data(), name.size());
    return ELISA_RENDER_SCENE_OK;
}

// The number of bundle images the cooked slot materials of resident mesh
// `high:low` sample.
extern "C" int32_t elisa_render_scene_v1_snapshot_mesh_texture_count(uint64_t mesh_high, uint64_t mesh_low) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const SnapshotMeshAssetSlot* mesh = nullptr;
    const int32_t status = cooked_mesh_asset(state, mesh_high, mesh_low, mesh);
    return status == ELISA_RENDER_SCENE_OK ? int32_t(mesh->geometry.texture_sections.size()) : status;
}

// Registers image `index` of resident mesh `mesh_high:mesh_low` as texture
// `high:low`, read from the bundle the mesh loaded from, with the rules of
// elisa_render_scene_v1_register_snapshot_bundle_texture_asset. A bundle
// rewritten since the mesh loaded fails to load.
extern "C" int32_t elisa_render_scene_v1_register_snapshot_mesh_texture(
    uint64_t mesh_high, uint64_t mesh_low, uint32_t index, uint64_t high, uint64_t low) {
    if (high == 0 && low == 0) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (state.snapshot_transaction_active) return ELISA_RENDER_SCENE_BATCH_ACTIVE;
    const SnapshotMeshAssetSlot* mesh = nullptr;
    const int32_t status = cooked_mesh_asset(state, mesh_high, mesh_low, mesh);
    if (status != ELISA_RENDER_SCENE_OK) return status;
    if (index >= mesh->geometry.texture_sections.size()) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    return register_snapshot_bundle_texture_locked(state, high, low, mesh->requested_path, mesh->canonical_path,
        mesh->geometry.texture_sections[index], &mesh->geometry.texture_checksums[index]);
}

// Registers the cooked material of one slot as material `high:low`, with
// the same no-op and conflict rules as any material registration. Its
// images must already be registered (elisa_render_scene_v1_register_snapshot_mesh_texture).
extern "C" int32_t elisa_render_scene_v1_register_snapshot_mesh_material(
    uint64_t mesh_high, uint64_t mesh_low, uint32_t slot, uint64_t high, uint64_t low) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (state.snapshot_transaction_active) return ELISA_RENDER_SCENE_BATCH_ACTIVE;
    const SnapshotMeshAssetSlot* mesh = nullptr;
    const int32_t status = cooked_mesh_asset(state, mesh_high, mesh_low, mesh);
    if (status != ELISA_RENDER_SCENE_OK) return status;
    if (slot >= mesh->geometry.slot_materials.size()) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    SnapshotMaterialAssetSlot values;
    const int32_t resolved = cooked_slot_material_values(state, *mesh, slot, values);
    if (resolved != ELISA_RENDER_SCENE_OK) return resolved;
    bool created = false;
    return register_snapshot_material_locked(state, high, low, values, created);
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
    const SnapshotMeshAssetSlot* mesh = nullptr;
    const int32_t status = cooked_mesh_asset(state, mesh_high, mesh_low, mesh);
    if (status != ELISA_RENDER_SCENE_OK) return status;
    if (count == 0 || count != set.count || count != mesh->geometry.slot_materials.size()) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    // Every slot's images resolve before any material is created.
    std::array<SnapshotMaterialAssetSlot, MAX_SNAPSHOT_MATERIAL_SET_SLOTS> values{};
    for (uint32_t slot = 0; slot < count; ++slot) {
        const int32_t resolved = cooked_slot_material_values(state, *mesh, slot, values[slot]);
        if (resolved != ELISA_RENDER_SCENE_OK) return resolved;
    }
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
            set.material_low[slot], values[slot], created[slot]);
        if (registered != ELISA_RENDER_SCENE_OK) return roll_back(registered);
    }
    const int32_t registered = register_snapshot_material_set_locked(state, set_high, set_low, set, count);
    return registered == ELISA_RENDER_SCENE_OK ? registered : roll_back(registered);
}
