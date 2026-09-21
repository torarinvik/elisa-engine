// Snapshot textures read from an image section of a cooked ELPK bundle. The
// bundle path resolves under the project root like any asset path, and its
// declared dependencies must be present (bundle_dependencies.h). The section
// is CRC-checked, bounded and decoded to CPU pixels before it is installed.
// Async callers decode on a worker; this compatibility entrypoint decodes on
// the owner thread. Material registration creates the GPU texture there.

extern "C" int32_t elisa_render_scene_v1_register_snapshot_bundle_texture_asset(
    uint64_t high, uint64_t low, const char* bundle_path, const char* section) {
    if ((high == 0 && low == 0) || bundle_path == nullptr ||
        !elisa::assets::valid_bundle_section_name(section)) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (state.snapshot_transaction_active) return ELISA_RENDER_SCENE_BATCH_ACTIVE;
    std::filesystem::path resolved_path;
    try {
        if (!elisa::assets::resolve_project_asset_path(bundle_path, resolved_path)) {
            return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
        }
    } catch (...) {
        return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
    }
    return register_snapshot_bundle_texture_locked(state, high, low, bundle_path, resolved_path, section, nullptr);
}
