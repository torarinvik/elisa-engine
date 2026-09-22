#pragma once

#include "probe_core.h"
#include "wiScene.h"

#include <array>
#include <cmath>
#include <cstdint>

namespace probe {

enum class EffectKind : uint8_t { Emitter, Decal };

struct EffectHandle {
    uint32_t slot = UINT32_MAX;
    uint32_t generation = 0;
    uintptr_t owner = 0;
    EffectKind kind = EffectKind::Emitter;
};

struct EmitterDesc {
    uint32_t max_particles = 256;
    float count = 8.0f;
    float lifetime = 1.0f;
    float size = 1.0f;
    XMFLOAT3 velocity = XMFLOAT3(0, 1, 0);
};

struct DecalDesc {
    XMFLOAT4 color = XMFLOAT4(1, 1, 1, 1);
    float range = 1.0f;
    float slope_blend = 0.0f;
};

class EffectBridge {
public:
    static constexpr uint32_t MAX_EFFECTS = 32;
    static constexpr uint32_t MAX_PARTICLES = 65536;
    static constexpr float MAX_VELOCITY = 10000.0f;

    explicit EffectBridge(wi::scene::Scene& scene)
        : scene_(scene), owner_(reinterpret_cast<uintptr_t>(this)) {}
    EffectBridge(const EffectBridge&) = delete;

    EffectHandle create_emitter(const EmitterDesc& desc, const XMFLOAT3& position) {
        if (!valid(desc)) return {};
        const uint32_t slot = free_slot();
        if (slot == MAX_EFFECTS) return {};
        const auto entity = scene_.Entity_CreateEmitter("elisa_effect_emitter", position);
        auto* emitter = scene_.emitters.GetComponent(entity);
        if (emitter == nullptr) return {};
        emitter->SetMaxParticleCount(desc.max_particles);
        emitter->count = desc.count;
        emitter->life = desc.lifetime;
        emitter->size = desc.size;
        emitter->velocity = desc.velocity;
        Entry& entry = entries_[slot];
        if (!activate(entry, entity, EffectKind::Emitter)) {
            scene_.Entity_Remove(entity);
            return {};
        }
        return {slot, entry.generation, owner_, EffectKind::Emitter};
    }

    EffectHandle create_decal(const DecalDesc& desc, const XMFLOAT3& position) {
        if (!valid(desc)) return {};
        const uint32_t slot = free_slot();
        if (slot == MAX_EFFECTS) return {};
        const auto entity = scene_.Entity_CreateDecal("elisa_effect_decal", "", "");
        auto* decal = scene_.decals.GetComponent(entity);
        if (decal == nullptr) return {};
        decal->color = desc.color;
        decal->range = desc.range;
        decal->slopeBlendPower = desc.slope_blend;
        auto* transform = scene_.transforms.GetComponent(entity);
        if (transform != nullptr) {
            transform->translation_local = position;
            transform->UpdateTransform();
        }
        Entry& entry = entries_[slot];
        if (!activate(entry, entity, EffectKind::Decal)) {
            scene_.Entity_Remove(entity);
            return {};
        }
        return {slot, entry.generation, owner_, EffectKind::Decal};
    }

    bool tick(EffectHandle handle, float dt) {
        Entry* entry = get(handle);
        if (entry == nullptr || handle.kind != EffectKind::Emitter || !std::isfinite(dt) || dt < 0.0f) return false;
        auto* emitter = scene_.emitters.GetComponent(entry->entity);
        auto* transform = scene_.transforms.GetComponent(entry->entity);
        if (emitter == nullptr || transform == nullptr) return false;
        emitter->UpdateCPU(*transform, dt);
        return true;
    }

    bool update_decal(EffectHandle handle, const DecalDesc& desc) {
        Entry* entry = get(handle);
        if (entry == nullptr || handle.kind != EffectKind::Decal || !valid(desc)) return false;
        auto* decal = scene_.decals.GetComponent(entry->entity);
        if (decal == nullptr) return false;
        decal->color = desc.color;
        decal->range = desc.range;
        decal->slopeBlendPower = desc.slope_blend;
        return true;
    }

    bool destroy(EffectHandle handle) {
        Entry* entry = get(handle);
        if (entry == nullptr) return false;
        scene_.Entity_Remove(entry->entity);
        entry->live = false;
        return true;
    }

    wi::ecs::Entity entity(EffectHandle handle) const {
        const Entry* entry = get(handle);
        return entry == nullptr ? wi::ecs::INVALID_ENTITY : entry->entity;
    }

private:
    struct Entry {
        wi::ecs::Entity entity = wi::ecs::INVALID_ENTITY;
        uint32_t generation = 0;
        bool live = false;
        EffectKind kind = EffectKind::Emitter;
    };

    static bool valid(const EmitterDesc& desc) {
        return desc.max_particles > 0 && desc.max_particles <= MAX_PARTICLES &&
            std::isfinite(desc.count) && desc.count >= 0.0f && desc.count <= desc.max_particles &&
            std::isfinite(desc.lifetime) && desc.lifetime > 0.0f && std::isfinite(desc.size) && desc.size > 0.0f &&
            std::isfinite(desc.velocity.x) && std::isfinite(desc.velocity.y) && std::isfinite(desc.velocity.z) &&
            std::abs(desc.velocity.x) <= MAX_VELOCITY && std::abs(desc.velocity.y) <= MAX_VELOCITY &&
            std::abs(desc.velocity.z) <= MAX_VELOCITY;
    }
    static bool valid(const DecalDesc& desc) {
        return std::isfinite(desc.color.x) && std::isfinite(desc.color.y) && std::isfinite(desc.color.z) &&
            std::isfinite(desc.color.w) && desc.color.w >= 0.0f && desc.color.w <= 1.0f &&
            std::isfinite(desc.range) && desc.range > 0.0f && std::isfinite(desc.slope_blend) && desc.slope_blend >= 0.0f;
    }
    bool activate(Entry& entry, wi::ecs::Entity entity, EffectKind kind) {
        if (entry.generation == UINT32_MAX) return false;
        ++entry.generation;
        entry.entity = entity;
        entry.kind = kind;
        entry.live = true;
        return true;
    }
    Entry* get(EffectHandle handle) {
        return handle.owner == owner_ && handle.slot < MAX_EFFECTS && entries_[handle.slot].live &&
            entries_[handle.slot].generation == handle.generation && entries_[handle.slot].kind == handle.kind ? &entries_[handle.slot] : nullptr;
    }
    const Entry* get(EffectHandle handle) const {
        return handle.owner == owner_ && handle.slot < MAX_EFFECTS && entries_[handle.slot].live &&
            entries_[handle.slot].generation == handle.generation && entries_[handle.slot].kind == handle.kind ? &entries_[handle.slot] : nullptr;
    }
    uint32_t free_slot() const {
        for (uint32_t index = 0; index < MAX_EFFECTS; ++index) if (!entries_[index].live) return index;
        return MAX_EFFECTS;
    }

    wi::scene::Scene& scene_;
    uintptr_t owner_;
    std::array<Entry, MAX_EFFECTS> entries_{};
};

inline bool probe_effect_bridge(wi::scene::Scene& scene) {
    const size_t emitters_before = scene.emitters.GetCount();
    const size_t decals_before = scene.decals.GetCount();
    EffectBridge bridge(scene);
    const auto emitter = bridge.create_emitter(EmitterDesc{}, XMFLOAT3(0, 0, 0));
    DecalDesc decal_desc;
    decal_desc.color = XMFLOAT4(1, 0, 0, 0.5f);
    const auto decal = bridge.create_decal(decal_desc, XMFLOAT3(1, 0, 0));
    auto* decal_component = scene.decals.GetComponent(scene.decals.GetEntity(scene.decals.GetCount() - 1));
    if (!check(bridge.tick(emitter, 1.0f / 60.0f) && decal_component != nullptr,
        "effects update owned emitter and decal") ||
        !check(decal_component->color.w == 0.5f && !bridge.update_decal(
            EffectHandle{decal.slot, decal.generation, 0, EffectKind::Decal}, decal_desc),
            "effects apply decal policy and reject foreign handle")) return false;
    if (!check(bridge.destroy(emitter) && bridge.destroy(decal) &&
        scene.emitters.GetCount() == emitters_before && scene.decals.GetCount() == decals_before,
        "effects destroy pooled resources")) return false;
    return true;
}

} // namespace probe
