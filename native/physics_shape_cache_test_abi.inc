#if defined(ELISA_PHYSICS_TEST_PROBE)
extern "C" int32_t elisa_physics_v1_test_shape_cache_counts(uint64_t* hits,
    uint64_t* misses) {
    const int32_t owner_status = require_application_owner();
    if (owner_status != ELISA_PHYSICS_OK) return owner_status;
    if (hits == nullptr || misses == nullptr) return ELISA_PHYSICS_INVALID_ARGUMENT;
    const PhysicsService& state = physics_service();
    *hits = state.shape_cache_hits;
    *misses = state.shape_cache_misses;
    return ELISA_PHYSICS_OK;
}

extern "C" int32_t elisa_physics_v1_test_corrupt_shape_cache(void) {
    const int32_t owner_status = require_application_owner();
    if (owner_status != ELISA_PHYSICS_OK) return owner_status;
    std::filesystem::path root;
    if (!elisa::assets::project_asset_root(root)) return ELISA_PHYSICS_INVALID_STATE;
    const std::filesystem::path cache_dir = root / "build/cache/physics";
    std::error_code error;
    size_t corrupted = 0;
    for (std::filesystem::directory_iterator entry(cache_dir, error), end;
            !error && entry != end; entry.increment(error)) {
        if (!entry->is_regular_file(error) || error || entry->path().extension() != ".joltshape") {
            error.clear();
            continue;
        }
        const uint64_t size = entry->file_size(error);
        if (error || size == 0) { error.clear(); continue; }
        std::fstream file(entry->path(), std::ios::binary | std::ios::in | std::ios::out);
        if (!file) continue;
        file.seekg(-1, std::ios::end);
        char byte = 0;
        file.read(&byte, 1);
        if (!file) continue;
        byte = char(uint8_t(byte) ^ 0xffu);
        file.seekp(-1, std::ios::end);
        file.write(&byte, 1);
        file.flush();
        if (file) ++corrupted;
    }
    return !error && corrupted != 0 ? ELISA_PHYSICS_OK : ELISA_PHYSICS_BACKEND_FAILURE;
}
#endif
