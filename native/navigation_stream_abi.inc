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

// Find tile (tx, tz) in a blob `load` already validated; `at` is its data.
bool blob_tile(const std::vector<unsigned char>& blob, uint32_t tx, uint32_t tz,
    probe::nav::cook::TileRecord& record, size_t& at) {
    namespace ck = probe::nav::cook;
    ck::BlobHeader header;
    at = 0;
    if (!ck::get(blob, at, header)) return false;
    for (uint32_t i = 0; i < header.tile_count; ++i) {
        if (!ck::get(blob, at, record)) return false;
        if (record.tx == tx && record.tz == tz) return true;
        at += record.size;
    }
    return false;
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
    probe::nav::cook::TileRecord record;
    size_t at = 0;
    if (!blob_tile(cook_state().blobs[slot], tx, tz, record, at)) return ELISA_NAVIGATION_INVALID_ARGUMENT;
    *loaded = artifact->streamed_mesh()->getTileAt(static_cast<int>(tx), static_cast<int>(tz), 0) != nullptr ? 1 : 0;
    return ELISA_NAVIGATION_OK;
}

// Remove a loaded tile. Routes that crossed it end at its border afterwards.
int32_t elisa_navigation_v1_tile_unload(uint32_t slot, uint32_t generation, uint32_t tx, uint32_t tz) {
    Service& nav = service();
    std::lock_guard<std::mutex> lock(nav.mutex);
    probe::nav::NavMeshArtifact* artifact = streamed_artifact(nav, slot, generation);
    if (artifact == nullptr) return ELISA_NAVIGATION_STALE_HANDLE;
    probe::nav::cook::TileRecord record;
    size_t at = 0;
    if (!blob_tile(cook_state().blobs[slot], tx, tz, record, at)) return ELISA_NAVIGATION_INVALID_ARGUMENT;
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
    const std::vector<unsigned char>& blob = cook_state().blobs[slot];
    probe::nav::cook::TileRecord record;
    size_t at = 0;
    if (!blob_tile(blob, tx, tz, record, at)) return ELISA_NAVIGATION_INVALID_ARGUMENT;
    dtNavMesh* mesh = artifact->streamed_mesh();
    if (mesh->getTileAt(static_cast<int>(tx), static_cast<int>(tz), 0) != nullptr) return ELISA_NAVIGATION_INVALID_ARGUMENT;
    unsigned char* copy = static_cast<unsigned char*>(dtAlloc(record.size, DT_ALLOC_PERM));
    if (copy == nullptr) return ELISA_NAVIGATION_CAPACITY;
    std::memcpy(copy, blob.data() + at, record.size);
    if (dtStatusFailed(mesh->addTile(copy, static_cast<int>(record.size), DT_TILE_FREE_DATA, 0, nullptr))) {
        dtFree(copy);
        return ELISA_NAVIGATION_BAKE_FAILED;
    }
    return ELISA_NAVIGATION_OK;
}

} // extern "C"
