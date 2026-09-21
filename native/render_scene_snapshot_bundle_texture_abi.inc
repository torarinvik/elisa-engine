// Snapshot textures read from an image section of a cooked ELPK bundle. The
// bundle path resolves under the project root like any asset path, and its
// declared dependencies must be present (bundle_dependencies.h). The section
// is CRC-checked and its image header bounded before its bytes are retained;
// Wicked decodes them when a material that references the texture registers.

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
    const size_t existing = snapshot_texture_asset_slot(state, high, low);
    if (existing < MAX_SNAPSHOT_TEXTURE_ASSETS) {
        const SnapshotTextureAssetSlot& asset = state.snapshot_texture_assets[existing];
        return asset.canonical_path == resolved_path && asset.section == section
            ? ELISA_RENDER_SCENE_OK : ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    size_t free_slot = MAX_SNAPSHOT_TEXTURE_ASSETS;
    for (size_t index = 0; index < MAX_SNAPSHOT_TEXTURE_ASSETS; ++index) {
        if (!state.snapshot_texture_assets[index].live) {
            free_slot = index;
            break;
        }
    }
    if (free_slot == MAX_SNAPSHOT_TEXTURE_ASSETS) return ELISA_RENDER_SCENE_CAPACITY;
    elisa::assets::BundleTexture texture;
    std::string load_error;
    try {
        if (!elisa::assets::verify_bundle_dependencies(resolved_path, load_error) ||
            !elisa::assets::read_bundle_texture(resolved_path, section, texture, load_error)) {
            std::fprintf(stderr, "Elisa bundle texture load failed: %s\n", load_error.c_str());
            return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
        }
    } catch (...) {
        return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
    }
    if (texture.encoded.size() > MAX_SNAPSHOT_BUNDLE_TEXTURE_BYTES - state.snapshot_bundle_texture_bytes) {
        return ELISA_RENDER_SCENE_CAPACITY;
    }
    char checksum[9] = {};
    std::snprintf(checksum, sizeof(checksum), "%08x", texture.checksum);
    SnapshotTextureAssetSlot& asset = state.snapshot_texture_assets[free_slot];
    asset.high = high;
    asset.low = low;
    asset.section = section;
    asset.resource_stem = resolved_path.string() + "#" + asset.section + "-" + checksum;
    asset.extension = elisa::assets::encoded_image_extension(texture.header.format);
    asset.canonical_path = std::move(resolved_path);
    asset.encoded = std::move(texture.encoded);
    asset.live = true;
    state.snapshot_bundle_texture_bytes += asset.encoded.size();
    return ELISA_RENDER_SCENE_OK;
}
