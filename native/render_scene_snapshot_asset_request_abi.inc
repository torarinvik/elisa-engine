// Asynchronous snapshot asset requests. A request records the ID and path and
// returns without touching the filesystem. The worker thread resolves the
// path, checks the bundle's dependency closure, reads the mesh or texture
// section and decodes bundle images. Nothing becomes visible until the owner
// thread pumps: pump adopts finished loads into ordinary asset slots under
// the same byte budgets as synchronous registration. While a request is
// loading, a row that names the mesh, or a material that names the texture,
// fails with ELISA_RENDER_SCENE_ASSET_PENDING; the renderer substitutes
// nothing.

namespace {

constexpr size_t MAX_SNAPSHOT_ASSET_PATH_BYTES = 4096;

bool bounded_asset_path(const char* path) {
    if (path == nullptr || path[0] == '\0') return false;
    for (size_t index = 0; index <= MAX_SNAPSHOT_ASSET_PATH_BYTES; ++index) {
        if (path[index] == '\0') return true;
    }
    return false;
}

size_t snapshot_assets_claimed(const RenderSceneService& state, SnapshotAssetKind kind) {
    size_t claimed = 0;
    if (kind == SnapshotAssetKind::Mesh) {
        for (const SnapshotMeshAssetSlot& asset : state.snapshot_mesh_assets) claimed += asset.live ? 1 : 0;
    } else {
        for (const SnapshotTextureAssetSlot& asset : state.snapshot_texture_assets) claimed += asset.live ? 1 : 0;
    }
    for (const SnapshotAssetRequest& request : state.snapshot_asset_requests.requests) {
        claimed += request.live && request.kind == kind ? 1 : 0;
    }
    return claimed;
}

int32_t request_snapshot_asset(SnapshotAssetKind kind, uint64_t high, uint64_t low,
        const char* path, const char* section, int32_t priority) {
    if ((high == 0 && low == 0) || !bounded_asset_path(path) ||
        (kind == SnapshotAssetKind::Texture && !elisa::assets::valid_bundle_section_name(section))) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (state.snapshot_transaction_active) return ELISA_RENDER_SCENE_BATCH_ACTIVE;
    const std::string requested_path(path);
    const std::string requested_section(kind == SnapshotAssetKind::Texture ? section : "");
    // A resident asset matches by the path string it was registered with.
    if (kind == SnapshotAssetKind::Mesh) {
        const size_t existing = snapshot_mesh_asset_slot(state, high, low);
        if (existing < MAX_SNAPSHOT_MESH_ASSETS) {
            return state.snapshot_mesh_assets[existing].requested_path == requested_path
                ? ELISA_RENDER_SCENE_OK : ELISA_RENDER_SCENE_INVALID_ARGUMENT;
        }
    } else {
        const size_t existing = snapshot_texture_asset_slot(state, high, low);
        if (existing < MAX_SNAPSHOT_TEXTURE_ASSETS) {
            const SnapshotTextureAssetSlot& asset = state.snapshot_texture_assets[existing];
            return asset.requested_path == requested_path && asset.section == requested_section
                ? ELISA_RENDER_SCENE_OK : ELISA_RENDER_SCENE_INVALID_ARGUMENT;
        }
    }
    // A loading request with the same path coalesces; a failed one retries.
    const size_t pending = snapshot_asset_request_slot(state, kind, high, low);
    if (pending < MAX_SNAPSHOT_ASSET_REQUESTS) {
        SnapshotAssetRequest& request = state.snapshot_asset_requests.requests[pending];
        if (request.failure == ELISA_RENDER_SCENE_OK) {
            if (request.path != requested_path || request.section != requested_section) {
                return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
            }
            state.snapshot_asset_requests.worker.set_priority(request.serial, priority);
            return ELISA_RENDER_SCENE_OK;
        }
        request = SnapshotAssetRequest{};
    }
    const size_t kind_capacity = kind == SnapshotAssetKind::Mesh
        ? MAX_SNAPSHOT_MESH_ASSETS : MAX_SNAPSHOT_TEXTURE_ASSETS;
    if (snapshot_assets_claimed(state, kind) >= kind_capacity) return ELISA_RENDER_SCENE_CAPACITY;
    size_t free_request = MAX_SNAPSHOT_ASSET_REQUESTS;
    for (size_t index = 0; index < MAX_SNAPSHOT_ASSET_REQUESTS; ++index) {
        if (!state.snapshot_asset_requests.requests[index].live) {
            free_request = index;
            break;
        }
    }
    if (free_request == MAX_SNAPSHOT_ASSET_REQUESTS) return ELISA_RENDER_SCENE_CAPACITY;
    SnapshotAssetRequests& requests = state.snapshot_asset_requests;
    const uint64_t serial = requests.next_serial++;
    const std::thread::id owner_thread = state.owner_thread;
    elisa::assets::SerialJobWorker<SnapshotAssetLoad>& worker = requests.worker;
    try {
        if (!worker.submit(serial, priority, [&worker, kind, requested_path, requested_section, owner_thread] {
                return load_snapshot_asset(worker, kind, requested_path, requested_section, owner_thread);
            })) {
            return ELISA_RENDER_SCENE_CAPACITY;
        }
    } catch (...) {
        return ELISA_RENDER_SCENE_BACKEND_FAILED;
    }
    SnapshotAssetRequest& request = requests.requests[free_request];
    request.high = high;
    request.low = low;
    request.serial = serial;
    request.kind = kind;
    request.path = requested_path;
    request.section = requested_section;
    request.failure = ELISA_RENDER_SCENE_OK;
    request.live = true;
    return ELISA_RENDER_SCENE_OK;
}

bool valid_snapshot_asset_kind(int32_t kind) {
    return kind == ELISA_RENDER_SCENE_SNAPSHOT_ASSET_MESH || kind == ELISA_RENDER_SCENE_SNAPSHOT_ASSET_TEXTURE;
}

} // namespace

extern "C" int32_t elisa_render_scene_v1_request_snapshot_mesh_asset(
    uint64_t high, uint64_t low, const char* package_path) {
    return request_snapshot_asset(SnapshotAssetKind::Mesh, high, low, package_path, nullptr, 0);
}

extern "C" int32_t elisa_render_scene_v1_request_snapshot_mesh_asset_with_priority(
    uint64_t high, uint64_t low, const char* package_path, int32_t priority) {
    return request_snapshot_asset(SnapshotAssetKind::Mesh, high, low, package_path, nullptr, priority);
}

extern "C" int32_t elisa_render_scene_v1_request_snapshot_bundle_texture_asset(
    uint64_t high, uint64_t low, const char* bundle_path, const char* section) {
    return request_snapshot_asset(SnapshotAssetKind::Texture, high, low, bundle_path, section, 0);
}

extern "C" int32_t elisa_render_scene_v1_request_snapshot_bundle_texture_asset_with_priority(
    uint64_t high, uint64_t low, const char* bundle_path, const char* section, int32_t priority) {
    return request_snapshot_asset(SnapshotAssetKind::Texture, high, low, bundle_path, section, priority);
}

// Adopts up to `budget` finished loads, oldest first. A result whose request
// was unregistered in the meantime is already gone. A failed load, or one
// over a slot or byte budget, marks its request failed with that status.
extern "C" int32_t elisa_render_scene_v1_pump_snapshot_assets(uint32_t budget) {
    if (budget == 0 || budget > MAX_SNAPSHOT_ASSET_ADOPTIONS) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (state.snapshot_transaction_active) return ELISA_RENDER_SCENE_BATCH_ACTIVE;
    std::vector<std::pair<uint64_t, SnapshotAssetLoad>> finished;
    try {
        finished.reserve(budget);
        state.snapshot_asset_requests.worker.take(budget, finished);
    } catch (...) {
        return ELISA_RENDER_SCENE_BACKEND_FAILED;
    }
    int32_t adopted = 0;
    for (auto& [serial, load] : finished) {
        SnapshotAssetRequest* request = nullptr;
        for (SnapshotAssetRequest& candidate : state.snapshot_asset_requests.requests) {
            if (candidate.live && candidate.serial == serial) {
                request = &candidate;
                break;
            }
        }
        if (request == nullptr) continue;
        int32_t status = ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
        try {
            status = adopt_snapshot_asset(state, *request, load);
        } catch (...) {
            status = ELISA_RENDER_SCENE_ASSET_LOAD_FAILED;
        }
        if (status == ELISA_RENDER_SCENE_OK) {
            *request = SnapshotAssetRequest{};
            ++adopted;
            continue;
        }
        request->failure = status;
        std::fprintf(stderr, "Elisa snapshot asset request failed: %s\n",
            load.error.empty() ? "over a snapshot asset budget" : load.error.c_str());
    }
    return adopted;
}

// Resident, loading or absent; a failed request returns its failure status.
extern "C" int32_t elisa_render_scene_v1_snapshot_asset_state(int32_t kind, uint64_t high, uint64_t low) {
    if (!valid_snapshot_asset_kind(kind) || (high == 0 && low == 0)) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    const SnapshotAssetKind asset_kind = static_cast<SnapshotAssetKind>(kind);
    const bool resident = asset_kind == SnapshotAssetKind::Mesh
        ? snapshot_mesh_asset_slot(state, high, low) < MAX_SNAPSHOT_MESH_ASSETS
        : snapshot_texture_asset_slot(state, high, low) < MAX_SNAPSHOT_TEXTURE_ASSETS;
    if (resident) return ELISA_RENDER_SCENE_SNAPSHOT_ASSET_RESIDENT;
    const size_t slot = snapshot_asset_request_slot(state, asset_kind, high, low);
    if (slot == MAX_SNAPSHOT_ASSET_REQUESTS) return ELISA_RENDER_SCENE_SNAPSHOT_ASSET_ABSENT;
    const int32_t failure = state.snapshot_asset_requests.requests[slot].failure;
    return failure == ELISA_RENDER_SCENE_OK ? ELISA_RENDER_SCENE_SNAPSHOT_ASSET_LOADING : failure;
}

#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
// Test hooks. The worker has its own lock, so waiting never holds the
// service mutex.
extern "C" int32_t elisa_render_scene_v1_test_hold_snapshot_asset_worker(int32_t start, int32_t running) {
    service().snapshot_asset_requests.worker.hold(start != 0, running != 0);
    return ELISA_RENDER_SCENE_OK;
}

extern "C" uint64_t elisa_render_scene_v1_test_snapshot_asset_worker_started(void) {
    return service().snapshot_asset_requests.worker.started();
}

extern "C" uint64_t elisa_render_scene_v1_test_snapshot_asset_worker_finished(void) {
    return service().snapshot_asset_requests.worker.finished();
}

extern "C" int32_t elisa_render_scene_v1_test_wait_snapshot_asset_worker(uint64_t started, uint64_t finished) {
    return service().snapshot_asset_requests.worker.wait_until(
        started, finished, std::chrono::seconds(120)) ? 1 : 0;
}

// Live request entries plus jobs the worker still holds; zero means nothing
// leaked.
extern "C" uint64_t elisa_render_scene_v1_test_snapshot_asset_requests(void) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    uint64_t live = 0;
    for (const SnapshotAssetRequest& request : state.snapshot_asset_requests.requests) live += request.live ? 1 : 0;
    return live + state.snapshot_asset_requests.worker.outstanding();
}

extern "C" uint64_t elisa_render_scene_v1_test_snapshot_mesh_assets(void) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    uint64_t live = 0;
    for (const SnapshotMeshAssetSlot& asset : state.snapshot_mesh_assets) live += asset.live ? 1 : 0;
    return live;
}

extern "C" uint64_t elisa_render_scene_v1_test_snapshot_geometry_bytes(void) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    return state.snapshot_geometry_bytes;
}
#endif
