// N02: stream the tiles of a cooked mesh out and back in. Included after
// navigation_cook_abi.inc; a tile reloads from the blob its mesh was cooked
// from, so only cooked meshes stream. Detour salts a tile's polygon refs on
// every removal, so corridors and polygons found before an unload never
// resolve to the reloaded tile. Every entry point takes `nav.mutex`.

namespace {

// The cooked mesh behind a live handle, or null when the handle is stale or
// the slot was published by a plain bake.
probe::nav::NavMeshArtifact* streamed_artifact(Service& nav, uint32_t slot, uint32_t generation) {
    const probe::nav::TileHandle handle{slot, generation};
    probe::nav::NavMeshArtifact* artifact = nav.store.streamed(handle);
    if (artifact == nullptr) return nullptr;
    const CookState& state = cook_state();
    return state.blob_generation[slot] == generation && !state.blobs[slot].empty() ? artifact : nullptr;
}

// The bytes of tile (tx, tz) of a cooked slot: its latest rebuild, else its
// cooked tile. False outside the grid; `size` is 0 for a tile with no mesh.
bool tile_bytes(uint32_t slot, uint32_t tx, uint32_t tz, const unsigned char*& data, uint32_t& size) {
    namespace ck = probe::nav::cook;
    const CookState& state = cook_state();
    const std::vector<unsigned char>& blob = state.blobs[slot];
    data = nullptr;
    size = 0;
    ck::BlobHeader header;
    size_t at = 0;
    if (!ck::get(blob, at, header) || tx >= header.tiles_x || tz >= header.tiles_z) return false;
    for (const TileOverride& tile : state.cooks[slot].rebuilt) {
        if (tile.tx != tx || tile.tz != tz) continue;
        data = tile.data.data();
        size = static_cast<uint32_t>(tile.data.size());
        return true;
    }
    ck::TileRecord record;
    for (uint32_t i = 0; i < header.tile_count; ++i) {
        if (!ck::get(blob, at, record)) return false;
        if (record.tx == tx && record.tz == tz) {
            data = blob.data() + at;
            size = record.size;
            return true;
        }
        at += record.size;
    }
    return true;
}

// Add a copy of `size` tile bytes to `mesh`.
int32_t add_tile_copy(dtNavMesh* mesh, const unsigned char* data, uint32_t size) {
    unsigned char* copy = static_cast<unsigned char*>(dtAlloc(size, DT_ALLOC_PERM));
    if (copy == nullptr) return ELISA_NAVIGATION_CAPACITY;
    std::memcpy(copy, data, size);
    if (dtStatusFailed(mesh->addTile(copy, static_cast<int>(size), DT_TILE_FREE_DATA, 0, nullptr))) {
        dtFree(copy);
        return ELISA_NAVIGATION_BAKE_FAILED;
    }
    return ELISA_NAVIGATION_OK;
}

} // namespace

extern "C" {

// `loaded` is 1 while tile (tx, tz) of a cooked mesh is in it, 0 when it was
// unloaded. A tile the cook left empty, or outside the grid, is invalid.
int32_t elisa_navigation_v1_tile_loaded(uint32_t slot, uint32_t generation, uint32_t tx, uint32_t tz,
    int32_t* loaded) {
    if (loaded == nullptr) return ELISA_NAVIGATION_INVALID_ARGUMENT;
    *loaded = 0;
    Service& nav = service();
    std::lock_guard<std::mutex> lock(nav.mutex);
    probe::nav::NavMeshArtifact* artifact = streamed_artifact(nav, slot, generation);
    if (artifact == nullptr) return ELISA_NAVIGATION_STALE_HANDLE;
    const unsigned char* data = nullptr;
    uint32_t size = 0;
    if (!tile_bytes(slot, tx, tz, data, size) || size == 0) return ELISA_NAVIGATION_INVALID_ARGUMENT;
    *loaded = artifact->streamed_mesh()->getTileAt(static_cast<int>(tx), static_cast<int>(tz), 0) != nullptr ? 1 : 0;
    return ELISA_NAVIGATION_OK;
}

// Remove a loaded tile. Routes that crossed it end at its border afterwards.
int32_t elisa_navigation_v1_tile_unload(uint32_t slot, uint32_t generation, uint32_t tx, uint32_t tz) {
    Service& nav = service();
    std::lock_guard<std::mutex> lock(nav.mutex);
    probe::nav::NavMeshArtifact* artifact = streamed_artifact(nav, slot, generation);
    if (artifact == nullptr) return ELISA_NAVIGATION_STALE_HANDLE;
    const unsigned char* data = nullptr;
    uint32_t size = 0;
    if (!tile_bytes(slot, tx, tz, data, size) || size == 0) return ELISA_NAVIGATION_INVALID_ARGUMENT;
    dtNavMesh* mesh = artifact->streamed_mesh();
    const dtMeshTile* tile = mesh->getTileAt(static_cast<int>(tx), static_cast<int>(tz), 0);
    if (tile == nullptr) return ELISA_NAVIGATION_INVALID_ARGUMENT;
    return dtStatusSucceed(mesh->removeTile(mesh->getTileRef(tile), nullptr, nullptr))
        ? ELISA_NAVIGATION_OK : ELISA_NAVIGATION_BAKE_FAILED;
}

// Put an unloaded tile back from the cooked blob, re-linking its neighbours.
int32_t elisa_navigation_v1_tile_load(uint32_t slot, uint32_t generation, uint32_t tx, uint32_t tz) {
    Service& nav = service();
    std::lock_guard<std::mutex> lock(nav.mutex);
    probe::nav::NavMeshArtifact* artifact = streamed_artifact(nav, slot, generation);
    if (artifact == nullptr) return ELISA_NAVIGATION_STALE_HANDLE;
    const unsigned char* data = nullptr;
    uint32_t size = 0;
    if (!tile_bytes(slot, tx, tz, data, size) || size == 0) return ELISA_NAVIGATION_INVALID_ARGUMENT;
    dtNavMesh* mesh = artifact->streamed_mesh();
    if (mesh->getTileAt(static_cast<int>(tx), static_cast<int>(tz), 0) != nullptr) return ELISA_NAVIGATION_INVALID_ARGUMENT;
    return add_tile_copy(mesh, data, size);
}

// Rebuild tile (tx, tz) from the staged scene with the settings its mesh was
// cooked with, and swap it in if the tile is loaded (or was never cooked).
// An unloaded tile stays unloaded and reloads as the rebuild. `polygons` is
// the rebuilt tile's polygon count; 0 means the tile is now empty.
int32_t elisa_navigation_v1_tile_rebuild(uint32_t slot, uint32_t generation, uint32_t tx, uint32_t tz,
    uint32_t* polygons) {
    namespace ck = probe::nav::cook;
    if (polygons == nullptr) return ELISA_NAVIGATION_INVALID_ARGUMENT;
    *polygons = 0;
    Service& nav = service();
    std::lock_guard<std::mutex> lock(nav.mutex);
    probe::nav::NavMeshArtifact* artifact = streamed_artifact(nav, slot, generation);
    if (artifact == nullptr) return ELISA_NAVIGATION_STALE_HANDLE;
    const unsigned char* old_data = nullptr;
    uint32_t old_size = 0;
    if (!tile_bytes(slot, tx, tz, old_data, old_size)) return ELISA_NAVIGATION_INVALID_ARGUMENT;
    if (nav.boxes == 0) return ELISA_NAVIGATION_INVALID_ARGUMENT;
    CookState& state = cook_state();
    SlotCook& cooked = state.cooks[slot];
    ck::BlobHeader header;
    size_t at = 0;
    if (!ck::get(state.blobs[slot], at, header)) return ELISA_NAVIGATION_BAKE_FAILED;
    probe::nav::BakeInput input;
    input.vertices = nav.vertices.data();
    input.vertex_count = static_cast<int>(nav.vertices.size() / 3);
    input.indices = nav.indices.data();
    input.triangle_count = static_cast<int>(nav.indices.size() / 3);
    input.volumes = nav.volumes.data();
    input.volume_count = static_cast<int>(nav.volumes.size());
    input.links = nav.links.data();
    input.link_count = static_cast<int>(nav.links.size());
    input.agent = cooked.agent;
    input.cell_size = cooked.cell_size;
    input.cell_height = cooked.cell_height;
    std::vector<unsigned char> areas(input.triangle_count, 0);
    rcContext context(false);
    rcMarkWalkableTriangles(&context, input.agent.slope, input.vertices, input.vertex_count,
        input.indices, input.triangle_count, areas.data());
    // Keep the cooked grid in x/z; widen it vertically for the new geometry.
    float low[3], high[3];
    rcCalcBounds(input.vertices, input.vertex_count, low, high);
    const float origin[3] = {header.origin[0], std::min(header.origin[1], low[1] - 1.0f), header.origin[2]};
    cooked.top = std::max(cooked.top, high[1] + input.agent.height);
    std::vector<unsigned char> data;
    int count = 0;
    if (!ck::build_tile(input, areas, ck::tile_config(input, cooked.tile_cells), origin, cooked.top,
            static_cast<int>(tx), static_cast<int>(tz), data, count)) {
        return ELISA_NAVIGATION_BAKE_FAILED;
    }
    if (count > ck::MAX_TILE_POLYS - probe::nav::MAX_OFFMESH_LINKS) return ELISA_NAVIGATION_CAPACITY;
    dtNavMesh* mesh = artifact->streamed_mesh();
    const dtMeshTile* current = mesh->getTileAt(static_cast<int>(tx), static_cast<int>(tz), 0);
    const bool swap = current != nullptr || old_size == 0;
    if (current != nullptr && dtStatusFailed(mesh->removeTile(mesh->getTileRef(current), nullptr, nullptr))) {
        return ELISA_NAVIGATION_BAKE_FAILED;
    }
    if (swap && !data.empty()) {
        const int32_t added = add_tile_copy(mesh, data.data(), static_cast<uint32_t>(data.size()));
        if (added != ELISA_NAVIGATION_OK) return added;
    }
    bool replaced = false;
    for (TileOverride& tile : cooked.rebuilt) {
        if (tile.tx == tx && tile.tz == tz) { tile.data = data; replaced = true; }
    }
    if (!replaced) cooked.rebuilt.push_back({tx, tz, data});
    *polygons = static_cast<uint32_t>(count);
    return ELISA_NAVIGATION_OK;
}

} // extern "C"
