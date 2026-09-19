#pragma once

// Logical native handles for resources owned by one Wicked scene. The handle
// never exposes a vendor pointer: it carries a slot, generation, and owner
// identity. Destroying a resource invalidates the generation immediately;
// later GPU retirement can be layered underneath this logical contract.
#include "wiScene.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace probe {

struct NativeResourceHandle {
    static constexpr uint32_t INVALID_SLOT = UINT32_MAX;
    uint32_t slot = INVALID_SLOT;
    uint32_t generation = 0;
    uintptr_t owner = 0;
};

class NativeResourceRegistry {
public:
    static constexpr uint32_t MAX_RESOURCES = 64;

    explicit NativeResourceRegistry(wi::scene::Scene& scene)
        : scene_(scene), owner_(reinterpret_cast<uintptr_t>(&scene)) {}

    NativeResourceRegistry(const NativeResourceRegistry&) = delete;
    NativeResourceRegistry& operator=(const NativeResourceRegistry&) = delete;

    NativeResourceHandle create_cube(const std::string& name) {
        for (uint32_t slot = 0; slot < MAX_RESOURCES; ++slot) {
            Slot& state = slots_[slot];
            if (state.live || state.retired) {
                continue;
            }
            const wi::ecs::Entity entity = scene_.Entity_CreateCube(name);
            if (entity == wi::ecs::INVALID_ENTITY) {
                return {};
            }
            if (state.generation == UINT32_MAX) {
                scene_.Entity_Remove(entity);
                return {};
            }
            state.generation = state.generation == 0 ? 1 : state.generation + 1;
            state.entity = entity;
            state.live = true;
            return NativeResourceHandle{slot, state.generation, owner_};
        }
        return {};
    }

    bool is_live(NativeResourceHandle handle) const {
        return slot_valid(handle) && slots_[handle.slot].live &&
            slots_[handle.slot].generation == handle.generation;
    }

    wi::ecs::Entity resolve(NativeResourceHandle handle) const {
        return is_live(handle) ? slots_[handle.slot].entity : wi::ecs::INVALID_ENTITY;
    }

    bool destroy(NativeResourceHandle handle) {
        if (!is_live(handle)) {
            return false;
        }
        scene_.Entity_Remove(slots_[handle.slot].entity);
        slots_[handle.slot].live = false;
        slots_[handle.slot].entity = wi::ecs::INVALID_ENTITY;
        return true;
    }

    // Logical destruction is immediate, but the vendor entity remains in a
    // retirement list until the caller has waited for its GPU submission.
    bool destroy_deferred(NativeResourceHandle handle, uint64_t submission_serial = 0) {
        if (!is_live(handle)) {
            return false;
        }
        retired_.push_back({slots_[handle.slot].entity, handle.slot, submission_serial});
        slots_[handle.slot].live = false;
        slots_[handle.slot].retired = true;
        return true;
    }

    size_t pending_retirements() const {
        return retired_.size();
    }

    void collect_retired(uint64_t completed_serial = UINT64_MAX) {
        size_t write = 0;
        for (const Retired& resource : retired_) {
            if (resource.serial > completed_serial) {
                retired_[write++] = resource;
                continue;
            }
            scene_.Entity_Remove(resource.entity);
            slots_[resource.slot].entity = wi::ecs::INVALID_ENTITY;
            slots_[resource.slot].retired = false;
        }
        retired_.resize(write);
    }

private:
    struct Slot {
        wi::ecs::Entity entity = wi::ecs::INVALID_ENTITY;
        uint32_t generation = 0;
        bool live = false;
        bool retired = false;
    };

    struct Retired {
        wi::ecs::Entity entity = wi::ecs::INVALID_ENTITY;
        uint32_t slot = NativeResourceHandle::INVALID_SLOT;
        uint64_t serial = 0;
    };

    bool slot_valid(NativeResourceHandle handle) const {
        return handle.owner == owner_ && handle.slot < MAX_RESOURCES;
    }

    wi::scene::Scene& scene_;
    uintptr_t owner_;
    std::array<Slot, MAX_RESOURCES> slots_{};
    std::vector<Retired> retired_;
};

} // namespace probe
