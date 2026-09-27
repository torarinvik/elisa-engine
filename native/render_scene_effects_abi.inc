namespace {
// Slot zero is reserved as an invalid encoding, so the bit field must hold
// every slot number from 1 through MAX_EFFECTS.
constexpr unsigned effect_slot_bits_for_capacity(uint32_t capacity) {
    unsigned bits = 0;
    uint64_t encoded_slot_count = 1;
    while (encoded_slot_count <= capacity) {
        ++bits;
        encoded_slot_count <<= 1;
    }
    return bits;
}

constexpr unsigned EFFECT_SLOT_BITS =
    effect_slot_bits_for_capacity(probe::EffectBridge::MAX_EFFECTS);
constexpr unsigned EFFECT_KIND_BIT = EFFECT_SLOT_BITS;
constexpr unsigned EFFECT_GENERATION_SHIFT = EFFECT_KIND_BIT + 1;
constexpr uint64_t EFFECT_SLOT_MASK = (uint64_t(1) << EFFECT_SLOT_BITS) - 1;
constexpr uint64_t EFFECT_KIND_MASK = uint64_t(1) << EFFECT_KIND_BIT;
static_assert(EFFECT_SLOT_MASK >= probe::EffectBridge::MAX_EFFECTS,
    "effect handle slot bits must encode every one-based slot");

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

bool valid_effect_rotation(float x, float y, float z, float w) {
    if (!finite(x) || !finite(y) || !finite(z) || !finite(w)) return false;
    const double length = std::hypot(std::hypot(double(x), double(y)), std::hypot(double(z), double(w)));
    return std::isfinite(length) && length > 1.0e-6;
}

XMFLOAT4 normalize_effect_rotation(float x, float y, float z, float w) {
    const double length = std::hypot(std::hypot(double(x), double(y)), std::hypot(double(z), double(w)));
    return XMFLOAT4(float(double(x) / length), float(double(y) / length),
        float(double(z) / length), float(double(w) / length));
}

int32_t effect_failure(const RenderSceneService& state, const probe::EffectHandle& handle) {
    return state.effects->entity(handle) == wi::ecs::INVALID_ENTITY
        ? ELISA_RENDER_SCENE_UNKNOWN_HANDLE : ELISA_RENDER_SCENE_BACKEND_FAILED;
}

int32_t effect_create_failure(probe::EffectCreateStatus status) {
    switch (status) {
    case probe::EffectCreateStatus::InvalidValue: return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    case probe::EffectCreateStatus::Capacity: return ELISA_RENDER_SCENE_CAPACITY;
    case probe::EffectCreateStatus::BackendFailure: return ELISA_RENDER_SCENE_BACKEND_FAILED;
    case probe::EffectCreateStatus::GenerationExhausted: return ELISA_RENDER_SCENE_GENERATION_EXHAUSTED;
    case probe::EffectCreateStatus::Ok: return ELISA_RENDER_SCENE_BACKEND_FAILED;
    }
    return ELISA_RENDER_SCENE_BACKEND_FAILED;
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
    probe::EffectCreateStatus create_status = probe::EffectCreateStatus::Ok;
    const probe::EffectHandle handle = state.effects->create_emitter(
        desc, XMFLOAT3(px, py, pz), &create_status);
    const int64_t encoded = encode_effect_handle(handle);
    return encoded == 0 ? effect_create_failure(create_status) : encoded;
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

extern "C" int32_t elisa_render_scene_v1_update_effect_position(
    int64_t handle_value, float px, float py, float pz) {
    if (!valid_effect_position(px, py, pz)) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    probe::EffectHandle handle;
    if (!decode_effect_handle(state, handle_value, handle)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    return state.effects->update_position(handle, XMFLOAT3(px, py, pz))
        ? ELISA_RENDER_SCENE_OK : effect_failure(state, handle);
}

extern "C" int64_t elisa_render_scene_v1_create_effect_decal(
    float px, float py, float pz,
    float qx, float qy, float qz, float qw,
    float red, float green, float blue, float alpha,
    float range, float slope_blend) {
    if (!valid_effect_position(px, py, pz) || !valid_effect_rotation(qx, qy, qz, qw) ||
        !valid_color(red, green, blue, alpha) ||
        !finite(range) || range <= 0.0f || !finite(slope_blend) || slope_blend < 0.0f) {
        return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    }
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    probe::DecalDesc desc;
    desc.rotation = normalize_effect_rotation(qx, qy, qz, qw);
    desc.color = XMFLOAT4(red, green, blue, alpha);
    desc.range = range;
    desc.slope_blend = slope_blend;
    probe::EffectCreateStatus create_status = probe::EffectCreateStatus::Ok;
    const probe::EffectHandle handle = state.effects->create_decal(
        desc, XMFLOAT3(px, py, pz), &create_status);
    const int64_t encoded = encode_effect_handle(handle);
    return encoded == 0 ? effect_create_failure(create_status) : encoded;
}

extern "C" int32_t elisa_render_scene_v1_update_effect_decal(
    int64_t handle_value, float qx, float qy, float qz, float qw,
    float red, float green, float blue, float alpha,
    float range, float slope_blend) {
    if (!valid_effect_rotation(qx, qy, qz, qw) || !valid_color(red, green, blue, alpha) ||
        !finite(range) || range <= 0.0f ||
        !finite(slope_blend) || slope_blend < 0.0f) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return ELISA_RENDER_SCENE_NOT_INITIALIZED;
    if (!on_owner_thread(state)) return ELISA_RENDER_SCENE_WRONG_THREAD;
    probe::EffectHandle handle;
    if (!decode_effect_handle(state, handle_value, handle)) return ELISA_RENDER_SCENE_UNKNOWN_HANDLE;
    if (handle.kind != probe::EffectKind::Decal) return ELISA_RENDER_SCENE_INVALID_ARGUMENT;
    probe::DecalDesc desc;
    desc.rotation = normalize_effect_rotation(qx, qy, qz, qw);
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
    int64_t handle_value, float qx, float qy, float qz, float qw,
    float red, float green, float blue, float alpha,
    float range, float slope_blend) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return 0;
    probe::EffectHandle handle;
    if (!decode_effect_handle(state, handle_value, handle) || handle.kind != probe::EffectKind::Decal) return 0;
    const auto entity = state.effects->entity(handle);
    const auto* decal = state.scene->decals.GetComponent(entity);
    const auto* transform = state.scene->transforms.GetComponent(entity);
    if (decal == nullptr || transform == nullptr) return 0;
    const XMVECTOR expected_rotation = XMQuaternionNormalize(XMVectorSet(qx, qy, qz, qw));
    const XMVECTOR actual_rotation = XMQuaternionNormalize(XMLoadFloat4(&transform->rotation_local));
    const bool rotation_matches = XMVectorGetX(XMVector4Length(XMVectorSubtract(expected_rotation, actual_rotation))) < 1.0e-5f ||
        XMVectorGetX(XMVector4Length(XMVectorAdd(expected_rotation, actual_rotation))) < 1.0e-5f;
    return decal->color.x == red && decal->color.y == green && decal->color.z == blue &&
        decal->color.w == alpha && decal->range == range && decal->slopeBlendPower == slope_blend &&
        transform->scale_local.x == range * 0.5f && transform->scale_local.y == range * 0.5f &&
        transform->scale_local.z == range * 0.5f && rotation_matches ? 1 : 0;
}

extern "C" int32_t elisa_render_scene_v1_test_effect_position(
    int64_t handle_value, float px, float py, float pz) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized) return 0;
    probe::EffectHandle handle;
    if (!decode_effect_handle(state, handle_value, handle)) return 0;
    const auto entity = state.effects->entity(handle);
    const auto* transform = state.scene->transforms.GetComponent(entity);
    if (transform == nullptr) return 0;
    return transform->translation_local.x == px && transform->translation_local.y == py &&
        transform->translation_local.z == pz ? 1 : 0;
}

extern "C" int32_t elisa_render_scene_v1_test_effect_component_count(int32_t kind) {
    RenderSceneService& state = service();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (!state.initialized || state.scene == nullptr) return -1;
    if (kind == 0) return static_cast<int32_t>(state.scene->emitters.GetCount());
    if (kind == 1) return static_cast<int32_t>(state.scene->decals.GetCount());
    return -1;
}

#endif
