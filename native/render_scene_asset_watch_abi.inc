// A10 hot-reload watcher: the size and last-write time of a project asset
// file, so an Elisa watcher can see an edit without reading the file. The
// path is confined to the project root like every snapshot asset path; a
// missing, escaping or unreadable file reports ASSET_LOAD_FAILED. It needs
// no render scene and touches no scene state.
extern "C" int32_t elisa_render_scene_v1_asset_file_stamp(
    const char* asset_path, uint64_t* size, int64_t* modified_ns) {
    if (asset_path == nullptr || size == nullptr || modified_ns == nullptr) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    try {
        std::filesystem::path resolved;
        if (!elisa::assets::resolve_project_asset_path(asset_path, resolved)) {
            return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
        }
        std::error_code error;
        const uintmax_t bytes = std::filesystem::file_size(resolved, error);
        if (error) return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
        const auto written = std::filesystem::last_write_time(resolved, error);
        if (error) return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
        *size = static_cast<uint64_t>(bytes);
        *modified_ns = static_cast<int64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(written.time_since_epoch()).count());
        return ELISA_RENDER_SCENE_OK;
    } catch (...) {
        return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
    }
}
