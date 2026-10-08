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

// Physical footprint in KiB, as Activity Monitor reports it: resident and
// compressed pages plus device allocations charged to this task. The effect
// memory smoke compares it across scene restarts; -1 means unavailable.
#if defined(__APPLE__)
#include <mach/mach.h>
#include "process_vm_regions.h"
#include <cstdlib>
#endif
extern "C" int64_t elisa_render_scene_v1_test_footprint_kib(int32_t cycle) {
#if defined(__APPLE__)
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) != KERN_SUCCESS) {
        return -1;
    }
    const int64_t kib = static_cast<int64_t>(info.phys_footprint / 1024);
    std::fprintf(stderr, "effect memory cycle %d: phys_footprint %lld KiB\n", cycle, static_cast<long long>(kib));
    // Observe each accounting domain without waiting, draining queues or
    // advancing a frame: those actions would change the lifecycle under test.
    malloc_statistics_t heap{};
    malloc_zone_statistics(nullptr, &heap);
    const auto* device = wi::graphics::GetDevice();
    std::fprintf(stderr,
        "effect memory domains cycle %d: heap_in_use %llu heap_reserved %llu gpu_allocated %llu frame %llu pipeline_jobs %llu\n",
        cycle, static_cast<unsigned long long>(heap.size_in_use),
        static_cast<unsigned long long>(heap.size_allocated),
        static_cast<unsigned long long>(device == nullptr ? 0 : device->GetMemoryUsage().usage),
        static_cast<unsigned long long>(device == nullptr ? 0 : device->GetFrameCount()),
        static_cast<unsigned long long>(wi::renderer::IsPipelineCreationActive()));
    std::fprintf(stderr,
        "effect memory VM cycle %d: resident %llu internal %llu compressed %llu reusable %llu device %llu\n",
        cycle, static_cast<unsigned long long>(info.resident_size),
        static_cast<unsigned long long>(info.internal),
        static_cast<unsigned long long>(info.compressed),
        static_cast<unsigned long long>(info.reusable),
        static_cast<unsigned long long>(info.device));
    if (count >= TASK_VM_INFO_REV3_COUNT) {
        std::fprintf(stderr,
            "effect memory ledger cycle %d: graphics %lld graphics_compressed %lld media %lld purgeable %lld\n",
            cycle, static_cast<long long>(info.ledger_tag_graphics_footprint),
            static_cast<long long>(info.ledger_tag_graphics_footprint_compressed),
            static_cast<long long>(info.ledger_tag_media_footprint),
            static_cast<long long>(info.ledger_purgeable_nonvolatile));
    }
    if (const char* enabled = std::getenv("ELISA_EFFECT_MEMORY_VM_REGIONS"); enabled != nullptr && enabled[0] == '1' && enabled[1] == '\0') {
        // Optional map observation only; no queue drain or frame progress.
        const auto regions = elisa::diagnostics::process_vm_regions();
        std::fprintf(stderr, "effect memory VM regions cycle %d: complete %d status %d regions %u\n",
            cycle, regions.complete ? 1 : 0, regions.status, regions.visited);
        for (unsigned tag = 0; tag < elisa::diagnostics::VM_REGION_TAG_BUCKETS; ++tag) {
            const auto& totals = regions.tags[tag];
            if (totals.regions == 0) continue;
            std::fprintf(stderr, "effect memory VM tag cycle %d: tag %u virtual %llu resident %llu dirtied %llu regions %llu\n",
                cycle, tag, static_cast<unsigned long long>(totals.virtual_bytes),
                static_cast<unsigned long long>(totals.resident_bytes),
                static_cast<unsigned long long>(totals.dirtied_bytes),
                static_cast<unsigned long long>(totals.regions));
        }
    }
    return kib;
#else
    (void)cycle;
    return -1;
#endif
}

#endif
