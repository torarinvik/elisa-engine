#pragma once

// Shared private state for the split physics ABI translation units. This file
// is native-only; no Wicked or Jolt type crosses the C ABI.
#include "application_abi.h"
#include "physics_query_bridge.h"
#include "physics_service_abi.h"
#include "wiPhysics.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace elisa_physics_internal {

constexpr uint32_t MAX_BODIES = 64;
constexpr uint32_t MAX_SHAPES = 64;
constexpr uint32_t INVALID_SHAPE_SLOT = MAX_SHAPES;
constexpr size_t MAX_CONTACT_EVENTS = ELISA_PHYSICS_MAX_CONTACT_EVENTS;

struct BodySlot {
    wi::ecs::Entity entity = wi::ecs::INVALID_ENTITY;
    uint64_t generation = 0;
    uint64_t shape_generation = 0;
    uint32_t shape_slot = INVALID_SHAPE_SLOT;
    bool live = false;
};

struct ShapeSlot {
    wi::scene::RigidBodyPhysicsComponent backend_shape{};
    float dimension_x = 0.0f;
    float dimension_y = 0.0f;
    float dimension_z = 0.0f;
    uint64_t generation = 0;
    uint32_t body_references = 0;
    int32_t kind = ELISA_PHYSICS_SHAPE_BOX;
    bool live = false;
};

struct PhysicsService {
    std::unique_ptr<wi::scene::Scene> scene;
    std::unique_ptr<probe::PhysicsQueryBridge> query_bridge;
    std::unique_ptr<probe::PhysicsContactQueueListener> contact_listener;
    std::array<BodySlot, MAX_BODIES> bodies{};
    std::array<ShapeSlot, MAX_SHAPES> shapes{};
    std::array<probe::PhysicsContactEvent, MAX_CONTACT_EVENTS> pending_contacts{};
    probe::PhysicsContactQueue contact_queue;
    size_t pending_contact_count = 0;
    size_t pending_contact_dropped = 0;
    uint64_t world_generation = 0;
    uint64_t tick = 0;
    bool initialized = false;
    bool simulation_before = true;
    bool interpolation_before = true;
#if defined(ELISA_PHYSICS_TEST_PROBE)
    bool fail_next_initialize_after_scene = false;
#endif
};

inline PhysicsService& physics_service() {
    static PhysicsService* value = new PhysicsService();
    return *value;
}

inline int32_t require_application_owner() {
    const int32_t status = elisa_application_v1_validate_owner_thread();
    if (status == ELISA_APPLICATION_OK) return ELISA_PHYSICS_OK;
    if (status == ELISA_APPLICATION_WRONG_THREAD) return ELISA_PHYSICS_WRONG_THREAD;
    return ELISA_PHYSICS_INVALID_STATE;
}

inline int32_t require_world(uint64_t generation) {
    const int32_t owner_status = require_application_owner();
    if (owner_status != ELISA_PHYSICS_OK) return owner_status;
    const PhysicsService& state = physics_service();
    if (!state.initialized || state.scene == nullptr) return ELISA_PHYSICS_INVALID_STATE;
    if (generation != state.world_generation) return ELISA_PHYSICS_STALE_WORLD;
    return ELISA_PHYSICS_OK;
}

inline BodySlot* resolve_body(PhysicsService& state, uint32_t slot, uint64_t generation) {
    if (slot >= MAX_BODIES) return nullptr;
    BodySlot& body = state.bodies[slot];
    return body.live && body.generation == generation ? &body : nullptr;
}

} // namespace elisa_physics_internal
