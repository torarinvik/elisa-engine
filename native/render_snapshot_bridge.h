#pragma once

// Generic Wicked consumer for Elisa render snapshots. Gameplay references and
// render IDs stay separate: one gameplay row may own several native entities,
// and native entity handles never become gameplay identities.
#include "wiScene.h"
#include "probe_core.h"

#include <array>
#include <cstdint>
#include <string>

namespace probe {

struct RenderSnapshotItem {
    int64_t render_id = 0;
    float position[3] = {};
    float scale[3] = {1.0f, 1.0f, 1.0f};
};

class RenderSnapshotBridge {
public:
    static constexpr size_t MAX_ITEMS = 128;

    explicit RenderSnapshotBridge(wi::scene::Scene& scene) : scene_(scene) {}
    RenderSnapshotBridge(const RenderSnapshotBridge&) = delete;
    RenderSnapshotBridge& operator=(const RenderSnapshotBridge&) = delete;

    bool upsert(const RenderSnapshotItem& item) {
        if (item.render_id <= 0) return false;
        size_t slot = find(item.render_id);
        if (slot == MAX_ITEMS) {
            slot = find_free();
            if (slot == MAX_ITEMS) return false;
            const auto entity = scene_.Entity_CreateCube("elisa_render_" + std::to_string(item.render_id));
            if (entity == wi::ecs::INVALID_ENTITY) return false;
            slots_[slot] = Slot{item.render_id, entity, true};
        }
        auto* transform = scene_.transforms.GetComponent(slots_[slot].entity);
        if (transform == nullptr) return false;
        transform->translation_local = XMFLOAT3(item.position[0], item.position[1], item.position[2]);
        transform->scale_local = XMFLOAT3(item.scale[0], item.scale[1], item.scale[2]);
        transform->SetDirty();
        transform->UpdateTransform();
        return true;
    }

    bool remove(int64_t render_id) {
        const size_t slot = find(render_id);
        if (slot == MAX_ITEMS) return false;
        scene_.Entity_Remove(slots_[slot].entity);
        slots_[slot] = {};
        return true;
    }

    bool live(int64_t render_id) const { return find(render_id) < MAX_ITEMS; }
    size_t count() const {
        size_t result = 0;
        for (const Slot& slot : slots_) result += slot.live ? 1 : 0;
        return result;
    }

private:
    struct Slot {
        int64_t render_id = 0;
        wi::ecs::Entity entity = wi::ecs::INVALID_ENTITY;
        bool live = false;
    };

    size_t find(int64_t render_id) const {
        for (size_t index = 0; index < MAX_ITEMS; ++index) {
            if (slots_[index].live && slots_[index].render_id == render_id) return index;
        }
        return MAX_ITEMS;
    }

    size_t find_free() const {
        for (size_t index = 0; index < MAX_ITEMS; ++index) {
            if (!slots_[index].live) return index;
        }
        return MAX_ITEMS;
    }

    wi::scene::Scene& scene_;
    std::array<Slot, MAX_ITEMS> slots_{};
};

inline bool probe_render_snapshot_bridge(wi::scene::Scene& scene) {
    const size_t objects_before = scene.objects.GetCount();
    RenderSnapshotBridge bridge(scene);
    const RenderSnapshotItem first{101, {1.0f, 2.0f, 3.0f}, {1.0f, 1.0f, 1.0f}};
    const RenderSnapshotItem second{102, {-1.0f, 0.0f, 2.0f}, {0.5f, 0.5f, 0.5f}};
    if (!check(bridge.upsert(first), "render snapshot creates first row") ||
        !check(bridge.upsert(second), "render snapshot creates second fanout row") ||
        !check(bridge.count() == 2 && scene.objects.GetCount() == objects_before + 2,
            "render snapshot owns two native entities")) return false;
    const RenderSnapshotItem moved{101, {4.0f, 0.0f, 1.0f}, {2.0f, 2.0f, 2.0f}};
    if (!check(bridge.upsert(moved), "render snapshot updates by render ID") ||
        !check(bridge.remove(102), "render snapshot removes one fanout row") ||
        !check(!bridge.live(102) && bridge.live(101) && bridge.count() == 1,
            "render snapshot keeps independent row alive") ||
        !check(!bridge.remove(999), "render snapshot rejects unknown row") ||
        !check(bridge.remove(101) && bridge.count() == 0 && scene.objects.GetCount() == objects_before,
            "render snapshot releases all native rows")) return false;
    return true;
}

} // namespace probe
