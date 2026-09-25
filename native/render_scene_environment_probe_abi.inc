// Bounded, generation-checked Elisa ownership for Wicked environment probes.
namespace {
constexpr float MAX_ENVIRONMENT_PROBE_VIEW_DISTANCE = 1000000.0f;
constexpr float MAX_ENVIRONMENT_PROBE_UPDATE_INTERVAL = 3600.0f;
constexpr float MAX_ENVIRONMENT_PROBE_INFLUENCE_EXTENT = 1000000.0f;
constexpr int32_t MIN_ENVIRONMENT_PROBE_RESOLUTION = 16;
constexpr int32_t MAX_ENVIRONMENT_PROBE_RESOLUTION = 2048;

bool valid_environment_probe_values(
    float px, float py, float pz, float extent_x, float extent_y, float extent_z,
    int32_t resolution, float view_distance,
    float update_interval, int32_t realtime, int32_t multisampled) {
    if (!bounded(px) || !bounded(py) || !bounded(pz) ||
        !bounded(view_distance, MAX_ENVIRONMENT_PROBE_VIEW_DISTANCE) ||
        !bounded(update_interval, MAX_ENVIRONMENT_PROBE_UPDATE_INTERVAL) ||
        !bounded(extent_x, MAX_ENVIRONMENT_PROBE_INFLUENCE_EXTENT) ||
        !bounded(extent_y, MAX_ENVIRONMENT_PROBE_INFLUENCE_EXTENT) ||
        !bounded(extent_z, MAX_ENVIRONMENT_PROBE_INFLUENCE_EXTENT) ||
        (realtime != 0 && realtime != 1) || (multisampled != 0 && multisampled != 1)) return false;
    return resolution >= MIN_ENVIRONMENT_PROBE_RESOLUTION &&
        resolution <= MAX_ENVIRONMENT_PROBE_RESOLUTION && (resolution & (resolution - 1)) == 0 &&
        view_distance > 0.0f && extent_x > 0.0f && extent_y > 0.0f && extent_z > 0.0f &&
        update_interval >= 0.0f;
}

probe::NativeEnvironmentProbeDesc make_environment_probe_desc(
    float px, float py, float pz, float extent_x, float extent_y, float extent_z,
    int32_t resolution, float view_distance,
    float update_interval, int32_t realtime, int32_t multisampled) {
    probe::NativeEnvironmentProbeDesc desc;
    desc.position = probe::coordinates::to_wicked(XMFLOAT3(px, py, pz));
    desc.influence_extent = XMFLOAT3(extent_x, extent_y, extent_z);
    desc.resolution = uint32_t(resolution);
    desc.view_distance = view_distance;
    desc.update_interval = update_interval;
    desc.realtime = realtime != 0;
    desc.multisampled = multisampled != 0;
    return desc;
}

int64_t encode_environment_probe_handle(size_t slot, uint32_t generation) {
    return int64_t((uint64_t(generation) << ENVIRONMENT_PROBE_HANDLE_SLOT_BITS) |
        uint64_t(slot + 1));
}

size_t decode_environment_probe_handle(int64_t value, uint32_t& generation) {
    if (value <= 0) return MAX_RENDER_ENVIRONMENT_PROBES;
    const uint64_t encoded = uint64_t(value);
    const uint64_t slot = encoded & ENVIRONMENT_PROBE_HANDLE_SLOT_MASK;
    if (slot == 0 || slot > MAX_RENDER_ENVIRONMENT_PROBES) return MAX_RENDER_ENVIRONMENT_PROBES;
    generation = uint32_t(encoded >> ENVIRONMENT_PROBE_HANDLE_SLOT_BITS);
    return size_t(slot - 1);
}

bool valid_environment_probe_handle(
    const RenderSceneService& state, int64_t value, size_t& slot_index) {
    uint32_t generation = 0;
    slot_index = decode_environment_probe_handle(value, generation);
    if (slot_index >= MAX_RENDER_ENVIRONMENT_PROBES || generation == 0 || state.lighting == nullptr) return false;
    const RenderEnvironmentProbeSlot& slot = state.environment_probes[slot_index];
    return slot.live && slot.generation == generation && slot.native.slot == slot_index &&
        state.lighting->live(slot.native);
}
}

extern "C" int64_t elisa_render_scene_v1_create_environment_probe(
    float px, float py, float pz, float extent_x, float extent_y, float extent_z,
    int32_t resolution, float view_distance,
    float update_interval, int32_t realtime, int32_t multisampled) {
    if (!valid_environment_probe_values(px, py, pz, extent_x, extent_y, extent_z,
        resolution, view_distance,
        update_interval, realtime, multisampled)) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (state.lighting == nullptr) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    const probe::NativeEnvironmentProbeDesc desc = make_environment_probe_desc(px, py, pz,
        extent_x, extent_y, extent_z, resolution, view_distance, update_interval, realtime, multisampled);
    const probe::NativeEnvironmentHandle native = state.lighting->create_environment(desc);
    if (native.owner == 0 || native.slot >= MAX_RENDER_ENVIRONMENT_PROBES) return ELISA_RENDER_SCENE_CAPACITY;
    RenderEnvironmentProbeSlot& slot = state.environment_probes[native.slot];
    slot.generation = slot.generation == UINT32_MAX ? 0 : slot.generation + 1;
    if (slot.generation == 0) {
        state.lighting->destroy_environment(native);
        return ELISA_RENDER_SCENE_GENERATION_EXHAUSTED;
    }
    slot.native = native;
    slot.live = true;
    return encode_environment_probe_handle(native.slot, slot.generation);
}

extern "C" int32_t elisa_render_scene_v1_update_environment_probe(
    int64_t handle, float px, float py, float pz, float extent_x, float extent_y,
    float extent_z, int32_t resolution,
    float view_distance, float update_interval, int32_t realtime, int32_t multisampled) {
    if (!valid_environment_probe_values(px, py, pz, extent_x, extent_y, extent_z,
        resolution, view_distance,
        update_interval, realtime, multisampled)) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_RENDER_ENVIRONMENT_PROBES;
    if (!valid_environment_probe_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    const probe::NativeEnvironmentProbeDesc desc = make_environment_probe_desc(px, py, pz,
        extent_x, extent_y, extent_z, resolution, view_distance, update_interval, realtime, multisampled);
    return state.lighting->update_environment(state.environment_probes[slot].native, desc) ?
        ELISA_RENDER_SCENE_OK : ELISA_RENDER_SCENE_BACKEND_FAILED;
}

extern "C" int32_t elisa_render_scene_v1_destroy_environment_probe(int64_t handle) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_RENDER_ENVIRONMENT_PROBES;
    if (!valid_environment_probe_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    if (!state.lighting->destroy_environment(state.environment_probes[slot].native)) {
        return ELISA_RENDER_SCENE_BACKEND_FAILED;
    }
    state.environment_probes[slot].live = false;
    return ELISA_RENDER_SCENE_OK;
}

extern "C" int32_t elisa_render_scene_v1_refresh_environment_probe(int64_t handle) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_RENDER_ENVIRONMENT_PROBES;
    if (!valid_environment_probe_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    return state.lighting->refresh_environment(state.environment_probes[slot].native) ?
        ELISA_RENDER_SCENE_OK : ELISA_RENDER_SCENE_BACKEND_FAILED;
}

#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
extern "C" int32_t elisa_render_scene_v1_test_environment_probe_matches(
    float px, float py, float pz, float extent_x, float extent_y, float extent_z,
    int32_t resolution,
    float view_distance, float update_interval, int32_t realtime, int32_t multisampled) {
    if (!valid_environment_probe_values(px, py, pz, extent_x, extent_y, extent_z,
        resolution, view_distance,
        update_interval, realtime, multisampled)) return 0;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.lighting == nullptr) return 0;
    const probe::NativeEnvironmentProbeDesc desc = make_environment_probe_desc(px, py, pz,
        extent_x, extent_y, extent_z, resolution, view_distance, update_interval, realtime, multisampled);
    for (const RenderEnvironmentProbeSlot& slot : state.environment_probes) {
        if (slot.live && state.lighting->environment_matches(slot.native, desc)) return 1;
    }
    return 0;
}

extern "C" int32_t elisa_render_scene_v1_test_environment_probe_count(void) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return -1;
    int32_t count = 0;
    for (const RenderEnvironmentProbeSlot& probe : state.environment_probes) {
        count += probe.live ? 1 : 0;
    }
    return count;
}

extern "C" int32_t elisa_render_scene_v1_test_environment_probe_clear_dirty(void) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state)) return 0;
    for (const RenderEnvironmentProbeSlot& slot : state.environment_probes) {
        if (!slot.live) continue;
        wi::scene::EnvironmentProbeComponent* probe =
            state.scene->probes.GetComponent(state.lighting->environment_entity(slot.native));
        if (probe == nullptr) return 0;
        probe->SetDirty(false);
        return 1;
    }
    return 0;
}

extern "C" int32_t elisa_render_scene_v1_test_environment_probe_is_dirty(void) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state)) return 0;
    for (const RenderEnvironmentProbeSlot& slot : state.environment_probes) {
        if (!slot.live) continue;
        const wi::scene::EnvironmentProbeComponent* probe =
            state.scene->probes.GetComponent(state.lighting->environment_entity(slot.native));
        return probe != nullptr && probe->IsDirty() ? 1 : 0;
    }
    return 0;
}
#endif
