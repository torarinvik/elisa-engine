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

bool snapshot_mesh_asset_in_use(const RenderSceneService& state, size_t slot) {
    if (slot >= MAX_SNAPSHOT_MESH_ASSETS || !state.snapshot_mesh_assets[slot].live) return false;
    const SnapshotMeshAssetSlot& asset = state.snapshot_mesh_assets[slot];
    for (const InstanceSlot& instance : state.instances) {
        if (instance.live && instance.mesh_high == asset.high && instance.mesh_low == asset.low) return true;
    }
    for (const SnapshotSharedMesh& shared : state.snapshot_shared_meshes) {
        if (shared.live && shared.mesh_high == asset.high && shared.mesh_low == asset.low) return true;
    }
    return false;
}

uint64_t next_snapshot_mesh_use_stamp(RenderSceneService& state) {
    if (state.snapshot_asset_use_clock == std::numeric_limits<uint64_t>::max()) {
        std::array<size_t, MAX_SNAPSHOT_MESH_ASSETS> live_slots{};
        size_t live_count = 0;
        for (size_t index = 0; index < MAX_SNAPSHOT_MESH_ASSETS; ++index) {
            if (state.snapshot_mesh_assets[index].live) live_slots[live_count++] = index;
        }
        std::sort(live_slots.begin(), live_slots.begin() + live_count,
            [&state](size_t left, size_t right) {
                const uint64_t left_use = state.snapshot_mesh_assets[left].last_used;
                const uint64_t right_use = state.snapshot_mesh_assets[right].last_used;
                return left_use == right_use ? left < right : left_use < right_use;
            });
        for (size_t rank = 0; rank < live_count; ++rank) {
            state.snapshot_mesh_assets[live_slots[rank]].last_used = rank + 1;
        }
        state.snapshot_asset_use_clock = live_count;
    }
    return ++state.snapshot_asset_use_clock;
}

void touch_snapshot_mesh_asset(RenderSceneService& state, size_t slot) {
    if (slot < MAX_SNAPSHOT_MESH_ASSETS && state.snapshot_mesh_assets[slot].live) {
        state.snapshot_mesh_assets[slot].last_used = next_snapshot_mesh_use_stamp(state);
    }
}

bool snapshot_mesh_path_is_lod_manifest(const std::string& path) {
    constexpr char SUFFIX[] = ".lod.json";
    return path.size() >= sizeof(SUFFIX) - 1 &&
        path.compare(path.size() - (sizeof(SUFFIX) - 1), sizeof(SUFFIX) - 1, SUFFIX) == 0;
}

const elisa::assets::CookedGeometry& snapshot_mesh_geometry(
        const SnapshotMeshAssetSlot& asset, size_t level) {
    return level == 0 ? asset.geometry : asset.additional_lod_geometry[level - 1];
}

bool snapshot_texture_asset_in_use(const RenderSceneService& state, size_t slot) {
    if (slot >= MAX_SNAPSHOT_TEXTURE_ASSETS || !state.snapshot_texture_assets[slot].live) return false;
    const SnapshotTextureAssetSlot& texture = state.snapshot_texture_assets[slot];
    for (const SnapshotMaterialAssetSlot& material : state.snapshot_material_assets) {
        if (!material.live) continue;
        for (size_t texture_slot = 0; texture_slot < SNAPSHOT_MATERIAL_TEXTURE_SLOTS; ++texture_slot) {
            if (material.texture_high[texture_slot] == texture.high &&
                material.texture_low[texture_slot] == texture.low) return true;
        }
    }
    return false;
}

uint64_t next_snapshot_texture_use_stamp(RenderSceneService& state) {
    if (state.snapshot_texture_use_clock == std::numeric_limits<uint64_t>::max()) {
        std::array<size_t, MAX_SNAPSHOT_TEXTURE_ASSETS> live_slots{};
        size_t live_count = 0;
        for (size_t index = 0; index < MAX_SNAPSHOT_TEXTURE_ASSETS; ++index) {
            if (state.snapshot_texture_assets[index].live) live_slots[live_count++] = index;
        }
        std::sort(live_slots.begin(), live_slots.begin() + live_count,
            [&state](size_t left, size_t right) {
                const uint64_t left_use = state.snapshot_texture_assets[left].last_used;
                const uint64_t right_use = state.snapshot_texture_assets[right].last_used;
                return left_use == right_use ? left < right : left_use < right_use;
            });
        for (size_t rank = 0; rank < live_count; ++rank) {
            state.snapshot_texture_assets[live_slots[rank]].last_used = rank + 1;
        }
        state.snapshot_texture_use_clock = live_count;
    }
    return ++state.snapshot_texture_use_clock;
}

void touch_snapshot_texture_asset(RenderSceneService& state, size_t slot) {
    if (slot < MAX_SNAPSHOT_TEXTURE_ASSETS && state.snapshot_texture_assets[slot].live) {
        state.snapshot_texture_assets[slot].last_used = next_snapshot_texture_use_stamp(state);
    }
}

void touch_snapshot_material_textures(RenderSceneService& state, uint64_t high, uint64_t low) {
    const size_t material_slot = snapshot_material_asset_slot(state, high, low);
    if (material_slot < MAX_SNAPSHOT_MATERIAL_ASSETS) {
        const SnapshotMaterialAssetSlot& material = state.snapshot_material_assets[material_slot];
        for (size_t texture_slot = 0; texture_slot < SNAPSHOT_MATERIAL_TEXTURE_SLOTS; ++texture_slot) {
            touch_snapshot_texture_asset(state,
                snapshot_texture_asset_slot(state, material.texture_high[texture_slot],
                    material.texture_low[texture_slot]));
        }
        return;
    }
    const size_t set_slot = snapshot_material_set_slot(state, high, low);
    if (set_slot == MAX_SNAPSHOT_MATERIAL_SETS) return;
    const SnapshotMaterialSet& set = state.snapshot_material_sets.sets[set_slot];
    for (uint32_t index = 0; index < set.count; ++index) {
        touch_snapshot_material_textures(state, set.material_high[index], set.material_low[index]);
    }
}

bool make_snapshot_texture_capacity(RenderSceneService& state, size_t source_bytes,
        size_t decoded_bytes, size_t& free_slot) {
    if (source_bytes > state.snapshot_bundle_texture_source_budget ||
        decoded_bytes > state.snapshot_decoded_texture_budget ||
        state.snapshot_bundle_texture_source_budget > MAX_SNAPSHOT_TEXTURE_SOURCE_BYTES ||
        state.snapshot_decoded_texture_budget > MAX_SNAPSHOT_DECODED_TEXTURE_BYTES) return false;
    free_slot = MAX_SNAPSHOT_TEXTURE_ASSETS;
    std::array<size_t, MAX_SNAPSHOT_TEXTURE_ASSETS> candidates{};
    size_t candidate_count = 0;
    for (size_t index = 0; index < MAX_SNAPSHOT_TEXTURE_ASSETS; ++index) {
        if (!state.snapshot_texture_assets[index].live) {
            if (free_slot == MAX_SNAPSHOT_TEXTURE_ASSETS) free_slot = index;
            continue;
        }
        if (!snapshot_texture_asset_in_use(state, index)) candidates[candidate_count++] = index;
    }
    std::sort(candidates.begin(), candidates.begin() + candidate_count,
        [&state](size_t left, size_t right) {
            const uint64_t left_use = state.snapshot_texture_assets[left].last_used;
            const uint64_t right_use = state.snapshot_texture_assets[right].last_used;
            return left_use == right_use ? left < right : left_use < right_use;
        });
    const size_t source_limit = state.snapshot_bundle_texture_source_budget - source_bytes;
    const size_t decoded_limit = state.snapshot_decoded_texture_budget - decoded_bytes;
    const size_t source_reclaim = state.snapshot_bundle_texture_source_bytes > source_limit
        ? state.snapshot_bundle_texture_source_bytes - source_limit : 0;
    const size_t decoded_reclaim = state.snapshot_decoded_texture_bytes > decoded_limit
        ? state.snapshot_decoded_texture_bytes - decoded_limit : 0;
    size_t source_reclaimed = 0;
    size_t decoded_reclaimed = 0;
    size_t victim_count = 0;
    while (victim_count < candidate_count &&
        (source_reclaimed < source_reclaim || decoded_reclaimed < decoded_reclaim ||
            (free_slot == MAX_SNAPSHOT_TEXTURE_ASSETS && victim_count == 0))) {
        const SnapshotTextureAssetSlot& victim = state.snapshot_texture_assets[candidates[victim_count++]];
        source_reclaimed += victim.source_bytes;
        decoded_reclaimed += victim.decoded.pixels.size();
    }
    if (source_reclaimed < source_reclaim || decoded_reclaimed < decoded_reclaim ||
        (free_slot == MAX_SNAPSHOT_TEXTURE_ASSETS && victim_count == 0)) return false;
    if (free_slot == MAX_SNAPSHOT_TEXTURE_ASSETS) free_slot = candidates[0];
    for (size_t index = 0; index < victim_count; ++index) {
        SnapshotTextureAssetSlot& victim = state.snapshot_texture_assets[candidates[index]];
        state.snapshot_bundle_texture_source_bytes -= victim.source_bytes;
        state.snapshot_decoded_texture_bytes -= victim.decoded.pixels.size();
        victim = SnapshotTextureAssetSlot{};
    }
    return true;
}

// Preflight the whole eviction before releasing anything. If live scene rows
// pin too much geometry, installation fails without changing residency.
bool make_snapshot_mesh_capacity(RenderSceneService& state, size_t bytes, size_t& free_slot) {
    if (bytes == 0 || bytes > state.snapshot_geometry_budget ||
        state.snapshot_geometry_budget > MAX_SNAPSHOT_GEOMETRY_BYTES) return false;

    free_slot = MAX_SNAPSHOT_MESH_ASSETS;
    std::array<size_t, MAX_SNAPSHOT_MESH_ASSETS> candidates{};
    size_t candidate_count = 0;
    for (size_t index = 0; index < MAX_SNAPSHOT_MESH_ASSETS; ++index) {
        if (!state.snapshot_mesh_assets[index].live) {
            if (free_slot == MAX_SNAPSHOT_MESH_ASSETS) free_slot = index;
            continue;
        }
        if (!snapshot_mesh_asset_in_use(state, index)) candidates[candidate_count++] = index;
    }
    std::sort(candidates.begin(), candidates.begin() + candidate_count,
        [&state](size_t left, size_t right) {
            const uint64_t left_use = state.snapshot_mesh_assets[left].last_used;
            const uint64_t right_use = state.snapshot_mesh_assets[right].last_used;
            return left_use == right_use ? left < right : left_use < right_use;
        });

    const size_t retained_budget = state.snapshot_geometry_budget - bytes;
    const size_t required_reclaim = state.snapshot_geometry_bytes > retained_budget
        ? state.snapshot_geometry_bytes - retained_budget : 0;
    size_t reclaimed = 0;
    size_t victim_count = 0;
    while (victim_count < candidate_count &&
        (reclaimed < required_reclaim || (free_slot == MAX_SNAPSHOT_MESH_ASSETS && victim_count == 0))) {
        reclaimed += state.snapshot_mesh_assets[candidates[victim_count++]].bytes;
    }
    if (reclaimed < required_reclaim ||
        (free_slot == MAX_SNAPSHOT_MESH_ASSETS && victim_count == 0)) return false;
    if (free_slot == MAX_SNAPSHOT_MESH_ASSETS) free_slot = candidates[0];

    for (size_t index = 0; index < victim_count; ++index) {
        SnapshotMeshAssetSlot& victim = state.snapshot_mesh_assets[candidates[index]];
        state.snapshot_geometry_bytes -= victim.bytes;
        victim = SnapshotMeshAssetSlot{};
    }
    return true;
}

int32_t install_snapshot_mesh_asset(RenderSceneService& state, uint64_t high, uint64_t low,
        std::string requested_path, std::filesystem::path canonical_path,
        elisa::assets::CookedGeometry geometry, size_t bytes) {
    size_t free_slot = MAX_SNAPSHOT_MESH_ASSETS;
    if (!make_snapshot_mesh_capacity(state, bytes, free_slot)) return ELISA_RENDER_SCENE_CAPACITY;
    SnapshotMeshAssetSlot& asset = state.snapshot_mesh_assets[free_slot];
    asset.high = high;
    asset.low = low;
    asset.requested_path = std::move(requested_path);
    asset.canonical_path = std::move(canonical_path);
    asset.geometry = std::move(geometry);
    asset.bytes = bytes;
    asset.last_used = next_snapshot_mesh_use_stamp(state);
    asset.live = true;
    state.snapshot_geometry_bytes += bytes;
    return ELISA_RENDER_SCENE_OK;
}

int32_t install_snapshot_lod_chain(RenderSceneService& state, uint64_t high, uint64_t low,
        std::string requested_path, std::filesystem::path canonical_path,
        elisa::assets::LodGeometryChain chain) {
    if (!chain.valid || chain.level_count < 2 || chain.level_count > elisa::assets::MAX_LOD_LEVELS) {
        return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
    }
    size_t free_slot = MAX_SNAPSHOT_MESH_ASSETS;
    if (!make_snapshot_mesh_capacity(state, chain.resident_bytes, free_slot)) return ELISA_RENDER_SCENE_CAPACITY;
    SnapshotMeshAssetSlot& asset = state.snapshot_mesh_assets[free_slot];
    asset.high = high;
    asset.low = low;
    asset.requested_path = std::move(requested_path);
    asset.canonical_path = std::move(canonical_path);
    asset.geometry = std::move(chain.geometry[0]);
    for (size_t index = 1; index < chain.level_count; ++index) {
        asset.additional_lod_geometry[index - 1] = std::move(chain.geometry[index]);
        asset.lod_error_budgets[index] = chain.manifest.levels[index].relative_error_budget;
    }
    asset.lod_error_budgets[0] = chain.manifest.levels[0].relative_error_budget;
    asset.lod_object_extent = chain.manifest.object_extent;
    asset.lod_level_count = chain.level_count;
    asset.bytes = chain.resident_bytes;
    asset.last_used = next_snapshot_mesh_use_stamp(state);
    asset.live = true;
    state.snapshot_geometry_bytes += asset.bytes;
    return ELISA_RENDER_SCENE_OK;
}

int32_t install_snapshot_bundle_texture_asset(RenderSceneService& state, uint64_t high, uint64_t low,
        std::string requested_path, std::filesystem::path canonical_path, std::string section,
        elisa::assets::BundleTexture texture, elisa::rendering::textures::DecodedImage decoded,
        bool decoded_on_worker) {
    size_t free_slot = MAX_SNAPSHOT_TEXTURE_ASSETS;
    for (size_t index = 0; index < MAX_SNAPSHOT_TEXTURE_ASSETS; ++index) {
        if (!state.snapshot_texture_assets[index].live) {
            free_slot = index;
            break;
        }
    }
    const size_t source_bytes = texture.encoded.size();
    const size_t decoded_bytes = decoded.pixels.size();
    const bool is_ktx2 = texture.header.format == elisa::assets::EncodedImageFormat::Ktx2;
    if (source_bytes == 0 || (!is_ktx2 && decoded_bytes == 0) || (is_ktx2 && decoded_bytes != 0)) {
        return ELISA_RENDER_SCENE_CAPACITY;
    }
    if (!is_ktx2) {
        const size_t pixel_count = static_cast<size_t>(decoded.width) * decoded.height;
        const size_t stored_channels = decoded.source_channels == 3 ? 4u :
            static_cast<size_t>(decoded.source_channels);
        if (decoded.width != texture.header.width || decoded.height != texture.header.height ||
            decoded.source_channels < 1 || decoded.source_channels > 4 ||
            pixel_count > MAX_SNAPSHOT_DECODED_TEXTURE_BYTES / stored_channels ||
            decoded_bytes != pixel_count * stored_channels) return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
    }
    if (!make_snapshot_texture_capacity(state, source_bytes, decoded_bytes, free_slot)) {
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
    asset.format = texture.header.format;
    asset.canonical_path = std::move(canonical_path);
    asset.decoded = std::move(decoded);
    if (is_ktx2) asset.encoded_ktx2 = std::move(texture.encoded);
    asset.source_bytes = source_bytes;
    asset.last_used = next_snapshot_texture_use_stamp(state);
    asset.live = true;
    asset.decoded_on_worker = decoded_on_worker;
    state.snapshot_bundle_texture_source_bytes += source_bytes;
    state.snapshot_decoded_texture_bytes += decoded_bytes;
    return ELISA_RENDER_SCENE_OK;
}

// Registers image `section` of the bundle at `canonical_path` as texture
// `high:low`, or confirms the same registration. A nonnull `checksum` is the
// section checksum the caller expects; a bundle rewritten since then fails
// to load. Callers hold the service mutex on the owner thread, outside a
// snapshot transaction.
int32_t register_snapshot_bundle_texture_locked(RenderSceneService& state, uint64_t high, uint64_t low,
        const std::string& requested_path, const std::filesystem::path& canonical_path,
        const std::string& section, const uint32_t* checksum) {
    const size_t existing = snapshot_texture_asset_slot(state, high, low);
    if (existing < MAX_SNAPSHOT_TEXTURE_ASSETS) {
        const SnapshotTextureAssetSlot& asset = state.snapshot_texture_assets[existing];
        return asset.canonical_path == canonical_path && asset.section == section
            ? ELISA_RENDER_SCENE_OK : ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    const int32_t claim = claim_snapshot_asset_id(state, SnapshotAssetKind::Texture, high, low);
    if (claim != ELISA_RENDER_SCENE_OK) return claim;
    const bool full = std::none_of(state.snapshot_texture_assets.begin(), state.snapshot_texture_assets.end(),
        [](const SnapshotTextureAssetSlot& asset) { return !asset.live; });
    if (full) return ELISA_RENDER_SCENE_CAPACITY;
    elisa::assets::BundleTexture texture;
    std::string load_error;
    try {
        if (!elisa::assets::verify_bundle_dependencies(canonical_path, load_error) ||
            !elisa::assets::read_bundle_texture(canonical_path, section.c_str(), texture, load_error)) {
            std::fprintf(stderr, "Elisa bundle texture load failed: %s\n", load_error.c_str());
            return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
        }
    } catch (...) {
        return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
    }
    elisa::rendering::textures::DecodedImage decoded;
    if (texture.header.format != elisa::assets::EncodedImageFormat::Ktx2 &&
        !elisa::rendering::textures::decode_image(texture.encoded, texture.header.width,
            texture.header.height, decoded)) {
        std::fprintf(stderr, "Elisa bundle texture decode failed: %s\n", canonical_path.string().c_str());
        return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
    }
    if (checksum != nullptr && texture.checksum != *checksum) {
        std::fprintf(stderr, "Elisa bundle texture load failed: %s changed since its mesh loaded\n",
            canonical_path.string().c_str());
        return ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
    }
    return install_snapshot_bundle_texture_asset(state, high, low, requested_path,
        canonical_path, section, std::move(texture), std::move(decoded), false);
}

// Runs on the worker. It resolves the path, checks the dependency closure and
// reads the mesh or texture section, all off the owner thread.
SnapshotAssetLoad load_snapshot_asset(elisa::assets::SerialJobWorker<SnapshotAssetLoad>& worker,
        SnapshotAssetKind kind, const std::string& path, const std::string& section,
        std::thread::id owner_thread) {
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
        auto cancellation_check = elisa::assets::package_read_checkpoint(worker);
        if (kind == SnapshotAssetKind::Mesh) {
            if (snapshot_mesh_path_is_lod_manifest(path)) {
                if (!elisa::assets::load_lod_geometry_chain(path, load.lod_chain, cancellation_check)) {
                    load.error = load.lod_chain.error;
                    return load;
                }
                load.bytes = load.lod_chain.resident_bytes;
                load.has_lod_chain = true;
            } else {
                if (!elisa::assets::load_cooked_geometry_asset(
                        load.canonical_path.string(), load.geometry, load.error,
                        cancellation_check)) return load;
                if (!cooked_geometry_bytes(load.geometry, load.bytes) || load.bytes == 0) {
                    load.status = ELISA_RENDER_SCENE_CAPACITY;
                    return load;
                }
            }
        } else {
            if (!elisa::assets::read_bundle_texture(load.canonical_path, section.c_str(),
                    load.texture, load.error, std::move(cancellation_check))) return load;
            if (load.texture.header.format != elisa::assets::EncodedImageFormat::Ktx2 &&
                !elisa::rendering::textures::decode_image(load.texture.encoded,
                    load.texture.header.width, load.texture.header.height, load.decoded_texture)) {
                load.error = "bundle texture image decode failed";
                return load;
            }
            load.decoded_on_worker = load.texture.header.format != elisa::assets::EncodedImageFormat::Ktx2 &&
                std::this_thread::get_id() != owner_thread;
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
        if (load.has_lod_chain) {
            return install_snapshot_lod_chain(state, request.high, request.low, request.path,
                std::move(load.canonical_path), std::move(load.lod_chain));
        }
        return install_snapshot_mesh_asset(state, request.high, request.low, request.path,
            std::move(load.canonical_path), std::move(load.geometry), load.bytes);
    }
    return install_snapshot_bundle_texture_asset(state, request.high, request.low, request.path,
        std::move(load.canonical_path), request.section, std::move(load.texture),
        std::move(load.decoded_texture), load.decoded_on_worker);
}

#include "render_scene_snapshot_material_internal.inc"
