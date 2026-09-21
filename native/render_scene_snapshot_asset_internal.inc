// Shared by synchronous registration and asynchronous requests: request
// lookups, the statuses a pending asset produces, slot installation, and the
// job the worker runs. The owner thread holds the service mutex for every
// function here except load_snapshot_asset, which runs on the worker and
// touches no service state.

size_t snapshot_asset_request_slot(const RenderSceneService& state,
        SnapshotAssetKind kind, uint64_t high, uint64_t low) {
    for (size_t index = 0; index < MAX_SNAPSHOT_ASSET_REQUESTS; ++index) {
        const SnapshotAssetRequest& request = state.snapshot_asset_requests.requests[index];
        if (request.live && request.kind == kind && request.high == high && request.low == low) return index;
    }
    return MAX_SNAPSHOT_ASSET_REQUESTS;
}

// Status for a row or material naming an asset with no live slot: pending
// while its request is loading, otherwise a load failure.
int32_t snapshot_missing_asset_status(const RenderSceneService& state,
        SnapshotAssetKind kind, uint64_t high, uint64_t low) {
    const size_t slot = snapshot_asset_request_slot(state, kind, high, low);
    return slot < MAX_SNAPSHOT_ASSET_REQUESTS &&
        state.snapshot_asset_requests.requests[slot].failure == ELISA_RENDER_SCENE_OK
        ? ELISA_RENDER_SCENE_ASSET_PENDING : ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
}

// A missing material or set is a load failure even when the mesh is still
// pending. A set must match a resident mesh's material slot count.
int32_t snapshot_row_asset_status(const RenderSceneService& state,
        uint64_t mesh_high, uint64_t mesh_low, uint64_t material_high, uint64_t material_low) {
    if (snapshot_material_asset_slot(state, material_high, material_low) == MAX_SNAPSHOT_MATERIAL_ASSETS &&
        snapshot_material_set_slot(state, material_high, material_low) == MAX_SNAPSHOT_MATERIAL_SETS) {
        return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
    }
    const size_t mesh = snapshot_mesh_asset_slot(state, mesh_high, mesh_low);
    if (mesh == MAX_SNAPSHOT_MESH_ASSETS) {
        return snapshot_missing_asset_status(state, SnapshotAssetKind::Mesh, mesh_high, mesh_low);
    }
    SnapshotSlotMaterials materials;
    return resolve_snapshot_slot_materials(state, material_high, material_low,
        state.snapshot_mesh_assets[mesh].geometry.material_slots, materials);
}

// Synchronous registration can't take an ID from a pending request. It
// replaces a failed one.
int32_t claim_snapshot_asset_id(RenderSceneService& state, SnapshotAssetKind kind, uint64_t high, uint64_t low) {
    const size_t slot = snapshot_asset_request_slot(state, kind, high, low);
    if (slot == MAX_SNAPSHOT_ASSET_REQUESTS) return ELISA_RENDER_SCENE_OK;
    SnapshotAssetRequest& request = state.snapshot_asset_requests.requests[slot];
    if (request.failure == ELISA_RENDER_SCENE_OK) return ELISA_RENDER_SCENE_ASSET_PENDING;
    request = SnapshotAssetRequest{};
    return ELISA_RENDER_SCENE_OK;
}

// Unregistering an ID with no live slot cancels its request: a queued job
// never starts, a running job's result is dropped, and a finished result is
// removed before adoption.
int32_t cancel_snapshot_asset_request(RenderSceneService& state, SnapshotAssetKind kind, uint64_t high, uint64_t low) {
    const size_t slot = snapshot_asset_request_slot(state, kind, high, low);
    if (slot == MAX_SNAPSHOT_ASSET_REQUESTS) return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
    SnapshotAssetRequest& request = state.snapshot_asset_requests.requests[slot];
    state.snapshot_asset_requests.worker.cancel(request.serial);
    request = SnapshotAssetRequest{};
    return ELISA_RENDER_SCENE_OK;
}

int32_t install_snapshot_mesh_asset(RenderSceneService& state, uint64_t high, uint64_t low,
        std::string requested_path, std::filesystem::path canonical_path,
        elisa::assets::CookedGeometry geometry, size_t bytes) {
    size_t free_slot = MAX_SNAPSHOT_MESH_ASSETS;
    for (size_t index = 0; index < MAX_SNAPSHOT_MESH_ASSETS; ++index) {
        if (!state.snapshot_mesh_assets[index].live) {
            free_slot = index;
            break;
        }
    }
    if (free_slot == MAX_SNAPSHOT_MESH_ASSETS || bytes == 0 ||
        bytes > MAX_SNAPSHOT_GEOMETRY_BYTES - state.snapshot_geometry_bytes) {
        return ELISA_RENDER_SCENE_CAPACITY;
    }
    SnapshotMeshAssetSlot& asset = state.snapshot_mesh_assets[free_slot];
    asset.high = high;
    asset.low = low;
    asset.requested_path = std::move(requested_path);
    asset.canonical_path = std::move(canonical_path);
    asset.geometry = std::move(geometry);
    asset.bytes = bytes;
    asset.live = true;
    state.snapshot_geometry_bytes += bytes;
    return ELISA_RENDER_SCENE_OK;
}

int32_t install_snapshot_bundle_texture_asset(RenderSceneService& state, uint64_t high, uint64_t low,
        std::string requested_path, std::filesystem::path canonical_path, std::string section,
        elisa::assets::BundleTexture texture) {
    size_t free_slot = MAX_SNAPSHOT_TEXTURE_ASSETS;
    for (size_t index = 0; index < MAX_SNAPSHOT_TEXTURE_ASSETS; ++index) {
        if (!state.snapshot_texture_assets[index].live) {
            free_slot = index;
            break;
        }
    }
    if (free_slot == MAX_SNAPSHOT_TEXTURE_ASSETS ||
        texture.encoded.size() > MAX_SNAPSHOT_BUNDLE_TEXTURE_BYTES - state.snapshot_bundle_texture_bytes) {
        return ELISA_RENDER_SCENE_CAPACITY;
    }
    char checksum[9] = {};
    std::snprintf(checksum, sizeof(checksum), "%08x", texture.checksum);
    SnapshotTextureAssetSlot& asset = state.snapshot_texture_assets[free_slot];
    asset.high = high;
    asset.low = low;
    asset.requested_path = std::move(requested_path);
    asset.section = std::move(section);
    asset.resource_stem = canonical_path.string() + "#" + asset.section + "-" + checksum;
    asset.extension = elisa::assets::encoded_image_extension(texture.header.format);
    asset.canonical_path = std::move(canonical_path);
    asset.encoded = std::move(texture.encoded);
    asset.live = true;
    state.snapshot_bundle_texture_bytes += asset.encoded.size();
    return ELISA_RENDER_SCENE_OK;
}

// Runs on the worker. It resolves the path, checks the dependency closure and
// reads the mesh or texture section, all off the owner thread.
SnapshotAssetLoad load_snapshot_asset(elisa::assets::SerialJobWorker<SnapshotAssetLoad>& worker,
        SnapshotAssetKind kind, const std::string& path, const std::string& section) {
    SnapshotAssetLoad load;
    try {
        if (!elisa::assets::resolve_project_asset_path(path.c_str(), load.canonical_path)) {
            load.error = "asset path is not a file under the project root";
            return load;
        }
        if (!elisa::assets::verify_bundle_dependencies(load.canonical_path, load.error)) return load;
        // A cancelled job stops here, before its section read. Test holds
        // park the job at this point.
        if (!worker.checkpoint()) return load;
        if (kind == SnapshotAssetKind::Mesh) {
            if (!elisa::assets::load_cooked_geometry_asset(
                    load.canonical_path.string(), load.geometry, load.error)) return load;
            if (!cooked_geometry_bytes(load.geometry, load.bytes) || load.bytes == 0) {
                load.status = ELISA_RENDER_SCENE_CAPACITY;
                return load;
            }
        } else if (!elisa::assets::read_bundle_texture(load.canonical_path, section.c_str(), load.texture, load.error)) {
            return load;
        }
        load.status = ELISA_RENDER_SCENE_OK;
    } catch (const std::exception& error) {
        load.error = error.what();
    } catch (...) {
        load.error = "unknown exception";
    }
    return load;
}

int32_t adopt_snapshot_asset(RenderSceneService& state, const SnapshotAssetRequest& request,
        SnapshotAssetLoad& load) {
    if (load.status != ELISA_RENDER_SCENE_OK) return load.status;
    if (request.kind == SnapshotAssetKind::Mesh) {
        return install_snapshot_mesh_asset(state, request.high, request.low, request.path,
            std::move(load.canonical_path), std::move(load.geometry), load.bytes);
    }
    return install_snapshot_bundle_texture_asset(state, request.high, request.low, request.path,
        std::move(load.canonical_path), request.section, std::move(load.texture));
}
