#pragma once

#include "probe_core.h"
#include "wiScene.h"
#include "coordinate_conventions.h"
#include "coordinate_transform_bridge.h"

#include <array>
#include <cmath>
#include <cstdint>

namespace probe {

struct PickGameplayRef {
    int64_t world_epoch = 0;
    int64_t entity_id = 0;
};

struct PickBinding {
    uint32_t slot = UINT32_MAX;
    uint32_t generation = 0;
    uintptr_t owner = 0;
};

class PickingBridge {
public:
    static constexpr uint32_t MAX_BINDINGS = 128;

    explicit PickingBridge(wi::scene::Scene& scene)
        : scene_(scene), owner_(reinterpret_cast<uintptr_t>(this)) {}
    PickingBridge(const PickingBridge&) = delete;

    PickBinding bind(wi::ecs::Entity native_entity, PickGameplayRef gameplay) {
        if (native_entity == wi::ecs::INVALID_ENTITY || gameplay.world_epoch <= 0 || gameplay.entity_id <= 0) return {};
        const uint32_t slot = free_slot();
        if (slot == MAX_BINDINGS) return {};
        Entry& entry = entries_[slot];
        if (entry.generation == UINT32_MAX) return {};
        ++entry.generation;
        entry.native_entity = native_entity;
        entry.gameplay = gameplay;
        entry.live = true;
        return {slot, entry.generation, owner_};
    }

    bool unbind(PickBinding binding) {
        Entry* entry = get(binding);
        if (entry == nullptr) return false;
        entry->live = false;
        return true;
    }

    bool pick(const wi::primitive::Ray& ray, uint32_t layer_mask,
        PickGameplayRef& gameplay, float& distance) const {
        wi::ecs::Entity native_entity = wi::ecs::INVALID_ENTITY;
        return pick(ray, layer_mask, gameplay, native_entity, distance);
    }

    // Resolve both the public gameplay identity and the private Wicked object
    // for callers that immediately apply a native selection effect.
    bool pick(const wi::primitive::Ray& ray, uint32_t layer_mask,
        PickGameplayRef& gameplay, wi::ecs::Entity& native_entity,
        float& distance) const {
        if (!finite_ray(ray)) return false;
        const auto result = scene_.Intersects(ray, wi::enums::FILTER_OBJECT_ALL, layer_mask);
        if (result.entity == wi::ecs::INVALID_ENTITY || !std::isfinite(result.distance)) return false;
        for (const Entry& entry : entries_) {
            if (entry.live && entry.native_entity == result.entity) {
                gameplay = entry.gameplay;
                native_entity = result.entity;
                distance = result.distance;
                return true;
            }
        }
        return false;
    }

private:
    struct Entry {
        wi::ecs::Entity native_entity = wi::ecs::INVALID_ENTITY;
        PickGameplayRef gameplay;
        uint32_t generation = 0;
        bool live = false;
    };

    static bool finite_ray(const wi::primitive::Ray& ray) {
        return std::isfinite(ray.origin.x) && std::isfinite(ray.origin.y) && std::isfinite(ray.origin.z) &&
            std::isfinite(ray.direction.x) && std::isfinite(ray.direction.y) && std::isfinite(ray.direction.z) &&
            std::isfinite(ray.TMin) && std::isfinite(ray.TMax) && ray.TMax > ray.TMin;
    }
    Entry* get(PickBinding binding) {
        return binding.owner == owner_ && binding.slot < MAX_BINDINGS && entries_[binding.slot].live &&
            entries_[binding.slot].generation == binding.generation ? &entries_[binding.slot] : nullptr;
    }
    uint32_t free_slot() const {
        for (uint32_t index = 0; index < MAX_BINDINGS; ++index) if (!entries_[index].live) return index;
        return MAX_BINDINGS;
    }

    wi::scene::Scene& scene_;
    uintptr_t owner_;
    std::array<Entry, MAX_BINDINGS> entries_{};
};

inline bool probe_picking_bridge(wi::scene::Scene& scene) {
    const auto target = scene.Entity_CreateCube("elisa_pick_target");
    auto* transform = scene.transforms.GetComponent(target);
    auto* layer = scene.layers.GetComponent(target);
    if (!check(target != wi::ecs::INVALID_ENTITY && transform != nullptr && layer != nullptr,
        "picking target creates components")) return false;
    const ElisaCoordinateProfile profile = elisa_coordinate_profile();
    const ElisaTransformPayload authored{{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f, 1.0f},
        {-0.5f, 0.25f, 1.5f}};
    if (!check(submit_elisa_transform(&profile, &authored, transform),
        "picking target submits signed nonuniform Elisa transform")) return false;
    transform->UpdateTransform();
    layer->layerMask = 1u << 5;
    scene.Update(0.0f);
    PickingBridge bridge(scene);
    const PickGameplayRef gameplay{7, 42};
    const auto binding = bridge.bind(target, gameplay);
    PickGameplayRef picked;
    float distance = 0.0f;
    const XMFLOAT3 authored_origin(0.0f, 0.0f, -4.0f);
    const XMFLOAT3 authored_direction(0.0f, 0.0f, 1.0f);
    const wi::primitive::Ray ray(coordinates::to_wicked(authored_origin),
        coordinates::to_wicked_direction(authored_direction), 0.0f, 20.0f);
    const wi::primitive::Ray nonuniform_miss(
        coordinates::to_wicked(XMFLOAT3(0.0f, 0.3f, -4.0f)),
        coordinates::to_wicked_direction(authored_direction), 0.0f, 20.0f);
    PickingBridge foreign(scene);
    if (!check(bridge.pick(ray, 1u << 5, picked, distance) &&
        picked.world_epoch == 7 && picked.entity_id == 42 && distance > 0.0f,
        "picking resolves signed-scale target from Elisa ray") ||
        !check(!bridge.pick(nonuniform_miss, 1u << 5, picked, distance),
            "picking applies nonuniform target scale") ||
        !check(!bridge.pick(ray, 1u << 4, picked, distance) && !foreign.unbind(binding),
            "picking enforces layer and owner boundaries")) return false;
    if (!check(bridge.unbind(binding) && !bridge.pick(ray, 1u << 5, picked, distance),
        "picking rejects stale selection")) return false;
    scene.Entity_Remove(target);
    scene.Update(0.0f);
    return check(scene.objects.GetComponent(target) == nullptr, "picking target unloads");
}

} // namespace probe
