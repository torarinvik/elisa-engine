// Public metadata for the source mesh placements retained by a cooked mesh.
// The geometry remains flattened for the renderer; these records let a client
// inspect the authored mesh/node grouping without re-reading the bundle.
constexpr uint32_t SNAPSHOT_MESH_PLACEMENT_COMPONENTS = 12;

extern "C" int32_t elisa_render_scene_v1_snapshot_mesh_placement_count(
    uint64_t mesh_high, uint64_t mesh_low) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const SnapshotMeshAssetSlot* mesh = nullptr;
    const int32_t status = cooked_mesh_asset(state, mesh_high, mesh_low, mesh);
    return status == ELISA_RENDER_SCENE_OK
        ? int32_t(mesh->geometry.mesh_placements.size()) : status;
}

extern "C" int32_t elisa_render_scene_v1_snapshot_mesh_placement(
    uint64_t mesh_high, uint64_t mesh_low, uint32_t index,
    uint32_t* mesh_index, uint32_t* node_index) {
    if (mesh_index == nullptr || node_index == nullptr) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const SnapshotMeshAssetSlot* mesh = nullptr;
    const int32_t status = cooked_mesh_asset(state, mesh_high, mesh_low, mesh);
    if (status != ELISA_RENDER_SCENE_OK) return status;
    if (index >= mesh->geometry.mesh_placements.size()) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    const auto& placement = mesh->geometry.mesh_placements[index];
    *mesh_index = placement.mesh;
    *node_index = placement.node;
    return ELISA_RENDER_SCENE_OK;
}

extern "C" float elisa_render_scene_v1_snapshot_mesh_placement_component(
    uint64_t mesh_high, uint64_t mesh_low, uint32_t index, uint32_t component) {
    constexpr float INVALID_COMPONENT = 0.0f;
    if (component >= SNAPSHOT_MESH_PLACEMENT_COMPONENTS) return INVALID_COMPONENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state)) return INVALID_COMPONENT;
    const SnapshotMeshAssetSlot* mesh = nullptr;
    const int32_t status = cooked_mesh_asset(state, mesh_high, mesh_low, mesh);
    if (status != ELISA_RENDER_SCENE_OK || index >= mesh->geometry.mesh_placements.size()) {
        return INVALID_COMPONENT;
    }
    return mesh->geometry.mesh_placements[index].transform[component];
}
