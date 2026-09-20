#pragma once

#include "probe_core.h"
#include "wiRenderPath3D.h"
#include "wiScene.h"

#include <array>
#include <cmath>
#include <cstdint>

namespace probe {

struct VisibilityHandle {
    uint32_t slot = UINT32_MAX;
    uint32_t generation = 0;
    uintptr_t owner = 0;
};

struct VisibilityDesc {
    float draw_distance = 1000.0f;
    float lod_bias = 0.0f;
    uint32_t layer_mask = ~0u;
    bool renderable = true;
};

class VisibilityLodBridge {
public:
    static constexpr uint32_t MAX_OBJECTS = 128;

    VisibilityLodBridge(wi::scene::Scene& scene, wi::RenderPath3D& path)
        : scene_(scene), path_(path), owner_(reinterpret_cast<uintptr_t>(this)) {}
    VisibilityLodBridge(const VisibilityLodBridge&) = delete;

    VisibilityHandle bind(wi::ecs::Entity entity, const VisibilityDesc& desc) {
        if (!valid(desc) || scene_.objects.GetComponent(entity) == nullptr) return {};
        const uint32_t slot = free_slot();
        if (slot == MAX_OBJECTS) return {};
        Entry& entry = entries_[slot];
        if (entry.generation == UINT32_MAX) return {};
        ++entry.generation;
        entry.entity = entity;
        entry.live = true;
        if (!apply(entry, desc)) {
            entry.live = false;
            return {};
        }
        return {slot, entry.generation, owner_};
    }

    bool update(VisibilityHandle handle, const VisibilityDesc& desc) {
        Entry* entry = get(handle);
        return entry != nullptr && valid(desc) && apply(*entry, desc);
    }

    bool destroy(VisibilityHandle handle) {
        Entry* entry = get(handle);
        if (entry == nullptr) return false;
        entry->live = false;
        return true;
    }

    void set_occlusion_culling(bool enabled) { path_.setOcclusionCullingEnabled(enabled); }
    bool occlusion_culling() const { return path_.getOcclusionCullingEnabled(); }

private:
    struct Entry {
        wi::ecs::Entity entity = wi::ecs::INVALID_ENTITY;
        uint32_t generation = 0;
        bool live = false;
    };

    static bool valid(const VisibilityDesc& desc) {
        return std::isfinite(desc.draw_distance) && desc.draw_distance > 0.0f &&
            std::isfinite(desc.lod_bias) && desc.lod_bias >= -16.0f && desc.lod_bias <= 16.0f;
    }
    bool apply(Entry& entry, const VisibilityDesc& desc) {
        auto* object = scene_.objects.GetComponent(entry.entity);
        if (object == nullptr) return false;
        object->draw_distance = desc.draw_distance;
        object->lod_bias = desc.lod_bias;
        object->filterMask = desc.layer_mask;
        object->SetRenderable(desc.renderable);
        return true;
    }
    Entry* get(VisibilityHandle handle) {
        return handle.owner == owner_ && handle.slot < MAX_OBJECTS && entries_[handle.slot].live &&
            entries_[handle.slot].generation == handle.generation ? &entries_[handle.slot] : nullptr;
    }
    uint32_t free_slot() const {
        for (uint32_t index = 0; index < MAX_OBJECTS; ++index) if (!entries_[index].live) return index;
        return MAX_OBJECTS;
    }

    wi::scene::Scene& scene_;
    wi::RenderPath3D& path_;
    uintptr_t owner_;
    std::array<Entry, MAX_OBJECTS> entries_{};
};

inline bool probe_visibility_lod(wi::scene::Scene& scene, wi::RenderPath3D& path) {
    const auto target = scene.Entity_CreateCube("elisa_visibility_target");
    VisibilityLodBridge bridge(scene, path);
    VisibilityDesc desc;
    desc.draw_distance = 24.0f;
    desc.lod_bias = 1.5f;
    desc.layer_mask = 1u << 6;
    const auto handle = bridge.bind(target, desc);
    auto* object = scene.objects.GetComponent(target);
    if (!check(handle.owner != 0 && object != nullptr && object->draw_distance == 24.0f &&
        object->lod_bias == 1.5f && object->filterMask == (1u << 6),
        "visibility applies distance, LOD, and layer policy") ||
        !check(bridge.update(handle, VisibilityDesc{12.0f, -1.0f, 1u << 6, false}) &&
            !object->IsRenderable(), "visibility updates render policy") ||
        !check(!bridge.update(VisibilityHandle{handle.slot, handle.generation, 0}, desc),
            "visibility rejects foreign handle")) return false;
    bridge.set_occlusion_culling(true);
    if (!check(bridge.occlusion_culling(), "visibility enables renderer occlusion path")) return false;
    if (!check(bridge.destroy(handle) && !bridge.update(handle, desc),
        "visibility retires stale object policy")) return false;
    scene.Entity_Remove(target);
    return check(scene.objects.GetComponent(target) == nullptr, "visibility target unloads");
}

} // namespace probe
