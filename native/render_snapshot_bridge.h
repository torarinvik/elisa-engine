#pragma once

// Generic Wicked consumer for Elisa render snapshots. Gameplay references and
// render IDs stay separate: one gameplay row may own several native entities,
// and native entity handles never become gameplay identities.
#include "wiScene.h"
#include "probe_core.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
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
        return apply_batch(&item, 1);
    }

    // Apply a frame extraction batch transactionally. All rows are validated
    // and staged before any native slot or transform is changed, so a bad
    // row cannot leave a partially submitted scene behind.
    bool apply_batch(const RenderSnapshotItem* items, size_t count) {
        if (count > MAX_ITEMS || (count != 0 && items == nullptr)) return false;
        if (count == 0) return true;

        std::array<Staged, MAX_ITEMS> staged{};
        size_t staged_count = 0;
        for (size_t index = 0; index < count; ++index) {
            const RenderSnapshotItem& item = items[index];
            if (!valid(item) || staged_find(staged, staged_count, item.render_id) != MAX_ITEMS) {
                return false;
            }
            const size_t existing = find(item.render_id);
            const size_t slot = existing == MAX_ITEMS
                ? find_free_staged(staged, staged_count)
                : existing;
            if (slot == MAX_ITEMS) return false;
            if (existing != MAX_ITEMS && scene_.transforms.GetComponent(slots_[existing].entity) == nullptr) {
                return false;
            }
            staged[staged_count++] = Staged{slot, item, existing == MAX_ITEMS, wi::ecs::INVALID_ENTITY};
        }

        for (size_t index = 0; index < staged_count; ++index) {
            Staged& row = staged[index];
            if (!row.is_new) continue;
            row.entity = scene_.Entity_CreateCube("elisa_render_" + std::to_string(row.item.render_id));
            if (row.entity == wi::ecs::INVALID_ENTITY ||
                scene_.transforms.GetComponent(row.entity) == nullptr) {
                rollback_created(staged, staged_count);
                return false;
            }
        }

        for (size_t index = 0; index < staged_count; ++index) {
            Staged& row = staged[index];
            if (row.is_new) slots_[row.slot] = Slot{row.item.render_id, row.entity, true};
            apply_transform(row.is_new ? row.entity : slots_[row.slot].entity, row.item);
        }
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

    struct Staged {
        size_t slot = MAX_ITEMS;
        RenderSnapshotItem item{};
        bool is_new = false;
        wi::ecs::Entity entity = wi::ecs::INVALID_ENTITY;
    };

    static bool valid(const RenderSnapshotItem& item) {
        if (item.render_id <= 0) return false;
        for (float value : item.position) {
            if (!std::isfinite(value)) return false;
        }
        for (float value : item.scale) {
            if (!std::isfinite(value) || value <= 0.0f) return false;
        }
        return true;
    }

    void apply_transform(wi::ecs::Entity entity, const RenderSnapshotItem& item) {
        auto* transform = scene_.transforms.GetComponent(entity);
        transform->translation_local = XMFLOAT3(item.position[0], item.position[1], item.position[2]);
        transform->scale_local = XMFLOAT3(item.scale[0], item.scale[1], item.scale[2]);
        transform->SetDirty();
        transform->UpdateTransform();
    }

    size_t staged_find(const std::array<Staged, MAX_ITEMS>& staged, size_t count, int64_t render_id) const {
        for (size_t index = 0; index < count; ++index) {
            if (staged[index].item.render_id == render_id) return staged[index].slot;
        }
        return MAX_ITEMS;
    }

    size_t find_free_staged(const std::array<Staged, MAX_ITEMS>& staged, size_t count) const {
        for (size_t slot = 0; slot < MAX_ITEMS; ++slot) {
            if (slots_[slot].live) continue;
            bool reserved = false;
            for (size_t index = 0; index < count; ++index) {
                if (staged[index].slot == slot) {
                    reserved = true;
                    break;
                }
            }
            if (!reserved) return slot;
        }
        return MAX_ITEMS;
    }

    void rollback_created(const std::array<Staged, MAX_ITEMS>& staged, size_t count) {
        for (size_t index = 0; index < count; ++index) {
            if (staged[index].is_new && staged[index].entity != wi::ecs::INVALID_ENTITY) {
                scene_.Entity_Remove(staged[index].entity);
            }
        }
    }

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
        !check(bridge.apply_batch(
            std::array<RenderSnapshotItem, 2>{
                RenderSnapshotItem{103, {2.0f, 1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},
                RenderSnapshotItem{104, {-2.0f, 1.0f, 0.0f}, {0.75f, 0.75f, 0.75f}},
            }.data(),
            2), "render snapshot applies a fanout batch") ||
        !check(bridge.count() == 4 && scene.objects.GetCount() == objects_before + 4,
            "render snapshot commits all batch rows") ||
        !check(!bridge.apply_batch(
            std::array<RenderSnapshotItem, 2>{
                RenderSnapshotItem{105, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},
                RenderSnapshotItem{106,
                    {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f},
                    {1.0f, 1.0f, 1.0f}},
            }.data(),
            2), "render snapshot rejects an invalid batch") ||
        !check(bridge.count() == 4 && !bridge.live(105) && scene.objects.GetCount() == objects_before + 4,
            "render snapshot rolls back an invalid batch") ||
        !check(!bridge.apply_batch(
            std::array<RenderSnapshotItem, 2>{
                RenderSnapshotItem{107, {0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},
                RenderSnapshotItem{107, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}},
            }.data(),
            2), "render snapshot rejects duplicate batch IDs") ||
        !check(bridge.count() == 4 && !bridge.live(107) && scene.objects.GetCount() == objects_before + 4,
            "render snapshot keeps duplicate batch atomic") ||
        !check(bridge.remove(102), "render snapshot removes one fanout row") ||
        !check(!bridge.live(102) && bridge.live(101) && bridge.count() == 3,
            "render snapshot keeps independent row alive") ||
        !check(bridge.remove(103) && bridge.remove(104),
            "render snapshot removes committed batch rows") ||
        !check(!bridge.remove(999), "render snapshot rejects unknown row") ||
        !check(bridge.remove(101) && bridge.count() == 0 && scene.objects.GetCount() == objects_before,
            "render snapshot releases all native rows")) return false;
    return true;
}

} // namespace probe
