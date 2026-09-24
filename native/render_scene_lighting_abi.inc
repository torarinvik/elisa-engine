// Public checked light handles for the active Wicked scene.
namespace {

constexpr unsigned LIGHT_HANDLE_SLOT_BITS = 6;
constexpr uint64_t LIGHT_HANDLE_SLOT_MASK = (uint64_t(1) << LIGHT_HANDLE_SLOT_BITS) - 1;

bool valid_light_values(
    int32_t kind,
    float red, float green, float blue,
    float px, float py, float pz,
    float dx, float dy, float dz,
    float intensity, float range, float inner_cone, float outer_cone,
    int32_t casts_shadow) {
    if (kind < 0 || kind > 2 || (casts_shadow != 0 && casts_shadow != 1)) return false;
    const float values[] = {red, green, blue, px, py, pz, dx, dy, dz,
        intensity, range, inner_cone, outer_cone};
    for (float value : values) {
        if (!bounded(value)) return false;
    }
    if (red < 0.0f || red > 1.0f || green < 0.0f || green > 1.0f ||
        blue < 0.0f || blue > 1.0f || intensity < 0.0f || range <= 0.0f ||
        inner_cone < 0.0f || outer_cone <= 0.0f || inner_cone > outer_cone) return false;
    const float direction_squared = dx * dx + dy * dy + dz * dz;
    return std::isfinite(direction_squared) && (kind == 1 || direction_squared > 1.0e-8f);
}

probe::NativeLightDesc make_light_desc(
    int32_t kind,
    float red, float green, float blue,
    float px, float py, float pz,
    float dx, float dy, float dz,
    float intensity, float range, float inner_cone, float outer_cone,
    int32_t casts_shadow) {
    probe::NativeLightDesc desc;
    desc.kind = kind == 0 ? probe::NativeLightKind::Directional :
        (kind == 2 ? probe::NativeLightKind::Spot : probe::NativeLightKind::Point);
    desc.color = XMFLOAT3(red, green, blue);
    desc.position = probe::coordinates::to_wicked(XMFLOAT3(px, py, pz));
    desc.direction = probe::coordinates::to_wicked_direction(XMFLOAT3(dx, dy, dz));
    desc.intensity = intensity;
    desc.range = range;
    desc.inner_cone = inner_cone;
    desc.outer_cone = outer_cone;
    desc.cast_shadow = casts_shadow != 0;
    return desc;
}

int64_t encode_light_handle(const probe::NativeLightHandle& handle) {
    return int64_t((uint64_t(handle.generation) << LIGHT_HANDLE_SLOT_BITS) |
        uint64_t(handle.slot + 1));
}

size_t decode_light_handle(int64_t value, uint32_t& generation) {
    if (value <= 0) return MAX_RENDER_LIGHTS;
    const uint64_t encoded = uint64_t(value);
    const uint64_t slot = encoded & LIGHT_HANDLE_SLOT_MASK;
    if (slot == 0 || slot > MAX_RENDER_LIGHTS) return MAX_RENDER_LIGHTS;
    generation = uint32_t(encoded >> LIGHT_HANDLE_SLOT_BITS);
    return size_t(slot - 1);
}

bool valid_light_handle(const RenderSceneService& state, int64_t value, size_t& slot) {
    uint32_t generation = 0;
    slot = decode_light_handle(value, generation);
    if (slot >= MAX_RENDER_LIGHTS || generation == 0 || state.lighting == nullptr) return false;
    const RenderLightSlot& light = state.lights[slot];
    return light.live && light.native.slot == slot && light.native.generation == generation &&
        state.lighting->live(light.native);
}

} // namespace

extern "C" int64_t elisa_render_scene_v1_create_light(
    int32_t kind,
    float red, float green, float blue,
    float px, float py, float pz,
    float dx, float dy, float dz,
    float intensity, float range, float inner_cone, float outer_cone,
    int32_t casts_shadow) {
    if (!valid_light_values(kind, red, green, blue, px, py, pz, dx, dy, dz,
        intensity, range, inner_cone, outer_cone, casts_shadow)) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    if (state.lighting == nullptr) return ELISA_RENDER_SCENE_BACKEND_FAILED;
    const probe::NativeLightDesc desc = make_light_desc(kind, red, green, blue, px, py, pz,
        dx, dy, dz, intensity, range, inner_cone, outer_cone, casts_shadow);
    const probe::NativeLightHandle native = state.lighting->create_light(desc);
    if (native.owner == 0 || native.slot >= MAX_RENDER_LIGHTS) {
        return ELISA_RENDER_SCENE_CAPACITY;
    }
    state.lights[native.slot] = RenderLightSlot{native, true};
    return encode_light_handle(native);
}

extern "C" int32_t elisa_render_scene_v1_update_light(
    int64_t handle, int32_t kind,
    float red, float green, float blue,
    float px, float py, float pz,
    float dx, float dy, float dz,
    float intensity, float range, float inner_cone, float outer_cone,
    int32_t casts_shadow) {
    if (!valid_light_values(kind, red, green, blue, px, py, pz, dx, dy, dz,
        intensity, range, inner_cone, outer_cone, casts_shadow)) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_RENDER_LIGHTS;
    if (!valid_light_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    const probe::NativeLightDesc desc = make_light_desc(kind, red, green, blue, px, py, pz,
        dx, dy, dz, intensity, range, inner_cone, outer_cone, casts_shadow);
    return state.lighting->update_light(state.lights[slot].native, desc) ?
        ELISA_RENDER_SCENE_OK : ELISA_RENDER_SCENE_BACKEND_FAILED;
}

extern "C" int32_t elisa_render_scene_v1_destroy_light(int64_t handle) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    size_t slot = MAX_RENDER_LIGHTS;
    if (!valid_light_handle(state, handle, slot)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    if (!state.lighting->destroy_light(state.lights[slot].native)) {
        return ELISA_RENDER_SCENE_BACKEND_FAILED;
    }
    state.lights[slot].live = false;
    return ELISA_RENDER_SCENE_OK;
}

#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
extern "C" int32_t elisa_render_scene_v1_test_light_matches(
    int32_t kind,
    float red, float green, float blue,
    float px, float py, float pz, float dx, float dy, float dz,
    float intensity, float range, float inner_cone, float outer_cone, int32_t casts_shadow) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || !on_owner_thread(state) || state.lighting == nullptr) return 0;
    const probe::NativeLightDesc desc = make_light_desc(kind, red, green, blue,
        px, py, pz, dx, dy, dz, intensity, range, inner_cone, outer_cone, casts_shadow);
    for (const RenderLightSlot& light : state.lights) {
        if (light.live && state.lighting->light_matches(light.native, desc)) return 1;
    }
    return 0;
}

extern "C" int32_t elisa_render_scene_v1_test_light_count(void) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return -1;
    int32_t count = 0;
    for (const RenderLightSlot& light : state.lights) count += light.live ? 1 : 0;
    return count;
}
#endif
