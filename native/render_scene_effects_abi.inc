namespace {
constexpr unsigned EFFECT_SLOT_BITS = 5;
constexpr unsigned EFFECT_KIND_BIT = EFFECT_SLOT_BITS;
constexpr unsigned EFFECT_GENERATION_SHIFT = EFFECT_SLOT_BITS + 1;
constexpr uint64_t EFFECT_SLOT_MASK = (uint64_t(1) << EFFECT_SLOT_BITS) - 1;
constexpr uint64_t EFFECT_KIND_MASK = uint64_t(1) << EFFECT_KIND_BIT;

int64_t encode_effect_handle(const probe::EffectHandle& handle) {
    if (handle.owner == 0 || handle.slot >= probe::EffectBridge::MAX_EFFECTS || handle.generation == 0) {
        return 0;
    }
    const uint64_t slot = uint64_t(handle.slot) + 1;
    const uint64_t kind = handle.kind == probe::EffectKind::Decal ? EFFECT_KIND_MASK : 0;
    const uint64_t value = (uint64_t(handle.generation) << EFFECT_GENERATION_SHIFT) | kind | slot;
    return value > uint64_t(INT64_MAX) ? 0 : int64_t(value);
}

bool decode_effect_handle(const RenderSceneService& state, int64_t value, probe::EffectHandle& handle) {
    if (value <= 0 || state.effects == nullptr) return false;
    const uint64_t encoded = uint64_t(value);
    const uint64_t slot = encoded & EFFECT_SLOT_MASK;
    const uint64_t generation = encoded >> EFFECT_GENERATION_SHIFT;
    if (slot == 0 || slot > probe::EffectBridge::MAX_EFFECTS || generation == 0 || generation > UINT32_MAX) {
        return false;
    }
    handle.slot = uint32_t(slot - 1);
    handle.generation = uint32_t(generation);
    handle.owner = reinterpret_cast<uintptr_t>(state.effects.get());
    handle.kind = (encoded & EFFECT_KIND_MASK) != 0 ? probe::EffectKind::Decal : probe::EffectKind::Emitter;
    return true;
}

bool valid_effect_position(float x, float y, float z) {
    return bounded(x) && bounded(y) && bounded(z);
}

int32_t effect_failure(const RenderSceneService& state, const probe::EffectHandle& handle) {
    return state.effects->entity(handle) == wi::ecs::INVALID_ENTITY
        ? ELISA_RENDER_SCENE_UNKNOWN_HANDLE : ELISA_RENDER_SCENE_BACKEND_FAILED;
}
}

extern "C" int64_t elisa_render_scene_v1_create_effect_emitter(
    float px, float py, float pz, uint32_t max_particles,
    float count, float lifetime, float size,
    float velocity_x, float velocity_y, float velocity_z) {
    if (!valid_effect_position(px, py, pz)) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    probe::EmitterDesc desc;
    desc.max_particles = max_particles;
    desc.count = count;
    desc.lifetime = lifetime;
    desc.size = size;
    desc.velocity = XMFLOAT3(velocity_x, velocity_y, velocity_z);
    const probe::EffectHandle handle = state.effects->create_emitter(desc, XMFLOAT3(px, py, pz));
    const int64_t encoded = encode_effect_handle(handle);
    return encoded == 0 ? ELISA_RENDER_SCENE_INVALID_ARGUMENT : encoded;
}

extern "C" int32_t elisa_render_scene_v1_tick_effect_emitter(int64_t handle_value, float delta_seconds) {
    if (!finite(delta_seconds) || delta_seconds < 0.0f) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    probe::EffectHandle handle;
    if (!decode_effect_handle(state, handle_value, handle)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    if (handle.kind != probe::EffectKind::Emitter) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    return state.effects->tick(handle, delta_seconds) ? ELISA_RENDER_SCENE_OK : effect_failure(state, handle);
}

extern "C" int64_t elisa_render_scene_v1_create_effect_decal(
    float px, float py, float pz,
    float red, float green, float blue, float alpha,
    float range, float slope_blend) {
    if (!valid_effect_position(px, py, pz) || !valid_color(red, green, blue, alpha) ||
        !finite(range) || range <= 0.0f || !finite(slope_blend) || slope_blend < 0.0f) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    probe::DecalDesc desc;
    desc.color = XMFLOAT4(red, green, blue, alpha);
    desc.range = range;
    desc.slope_blend = slope_blend;
    const probe::EffectHandle handle = state.effects->create_decal(desc, XMFLOAT3(px, py, pz));
    const int64_t encoded = encode_effect_handle(handle);
    return encoded == 0 ? ELISA_RENDER_SCENE_INVALID_ARGUMENT : encoded;
}

extern "C" int32_t elisa_render_scene_v1_update_effect_decal(
    int64_t handle_value, float red, float green, float blue, float alpha,
    float range, float slope_blend) {
    if (!valid_color(red, green, blue, alpha) || !finite(range) || range <= 0.0f ||
        !finite(slope_blend) || slope_blend < 0.0f) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    probe::EffectHandle handle;
    if (!decode_effect_handle(state, handle_value, handle)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    if (handle.kind != probe::EffectKind::Decal) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    probe::DecalDesc desc;
    desc.color = XMFLOAT4(red, green, blue, alpha);
    desc.range = range;
    desc.slope_blend = slope_blend;
    return state.effects->update_decal(handle, desc) ? ELISA_RENDER_SCENE_OK : effect_failure(state, handle);
}

extern "C" int32_t elisa_render_scene_v1_destroy_effect(int64_t handle_value) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    probe::EffectHandle handle;
    if (!decode_effect_handle(state, handle_value, handle)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    return state.effects->destroy(handle) ? ELISA_RENDER_SCENE_OK : effect_failure(state, handle);
}

#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
extern "C" int32_t elisa_render_scene_v1_test_effect_emitter(
    int64_t handle_value, uint32_t max_particles, float count, float lifetime, float size,
    float velocity_x, float velocity_y, float velocity_z) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return 0;
    probe::EffectHandle handle;
    if (!decode_effect_handle(state, handle_value, handle) || handle.kind != probe::EffectKind::Emitter) return 0;
    const auto entity = state.effects->entity(handle);
    const auto* emitter = state.scene->emitters.GetComponent(entity);
    if (emitter == nullptr) return 0;
    return emitter->GetMaxParticleCount() == max_particles && emitter->count == count &&
        emitter->life == lifetime && emitter->size == size && emitter->velocity.x == velocity_x &&
        emitter->velocity.y == velocity_y && emitter->velocity.z == velocity_z ? 1 : 0;
}

extern "C" int32_t elisa_render_scene_v1_test_effect_decal(
    int64_t handle_value, float red, float green, float blue, float alpha,
    float range, float slope_blend) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return 0;
    probe::EffectHandle handle;
    if (!decode_effect_handle(state, handle_value, handle) || handle.kind != probe::EffectKind::Decal) return 0;
    const auto entity = state.effects->entity(handle);
    const auto* decal = state.scene->decals.GetComponent(entity);
    if (decal == nullptr) return 0;
    return decal->color.x == red && decal->color.y == green && decal->color.z == blue &&
        decal->color.w == alpha && decal->range == range && decal->slopeBlendPower == slope_blend ? 1 : 0;
}
#endif
