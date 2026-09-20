#include "physics_service_abi.h"

#include "application_abi.h"
#include "coordinate_transform_bridge.h"
#include "wiPhysics.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <string>

namespace {

constexpr uint32_t MAX_BODIES = 64;
constexpr float MAX_POSITION = 1.0e6f;
constexpr float MAX_HALF_EXTENT = 1.0e4f;
constexpr float MAX_MASS = 1.0e8f;
constexpr float MAX_FIXED_DELTA = 1.0f / 30.0f;

struct BodySlot {
    wi::ecs::Entity entity = wi::ecs::INVALID_ENTITY;
    uint64_t generation = 0;
    bool live = false;
};

struct PhysicsService {
    std::unique_ptr<wi::scene::Scene> scene;
    std::array<BodySlot, MAX_BODIES> bodies{};
    uint64_t world_generation = 0;
    uint64_t tick = 0;
    bool initialized = false;
    bool simulation_before = true;
    bool interpolation_before = true;
#if defined(ELISA_PHYSICS_TEST_PROBE)
    bool fail_next_initialize_after_scene = false;
#endif
};

PhysicsService& physics_service() {
    static PhysicsService* value = new PhysicsService();
    return *value;
}

int32_t require_application_owner() {
    const int32_t status = elisa_application_v1_validate_owner_thread();
    if (status == ELISA_APPLICATION_OK) return ELISA_PHYSICS_OK;
    if (status == ELISA_APPLICATION_WRONG_THREAD) return ELISA_PHYSICS_WRONG_THREAD;
    return ELISA_PHYSICS_INVALID_STATE;
}

int32_t require_world(uint64_t generation) {
    const int32_t owner_status = require_application_owner();
    if (owner_status != ELISA_PHYSICS_OK) return owner_status;
    const PhysicsService& state = physics_service();
    if (!state.initialized || state.scene == nullptr) return ELISA_PHYSICS_INVALID_STATE;
    if (generation != state.world_generation) return ELISA_PHYSICS_STALE_WORLD;
    return ELISA_PHYSICS_OK;
}

BodySlot* resolve_body(PhysicsService& state, uint32_t slot, uint64_t generation) {
    if (slot >= MAX_BODIES) return nullptr;
    BodySlot& body = state.bodies[slot];
    return body.live && body.generation == generation ? &body : nullptr;
}

void shutdown_world() {
    PhysicsService& state = physics_service();
    if (!state.initialized) return;
    state.scene.reset();
    for (BodySlot& body : state.bodies) {
        body.entity = wi::ecs::INVALID_ENTITY;
        body.live = false;
    }
    state.tick = 0;
    state.initialized = false;
    wi::physics::SetSimulationEnabled(state.simulation_before);
    wi::physics::SetInterpolationEnabled(state.interpolation_before);
}

} // namespace

extern "C" int32_t elisa_physics_v1_initialize(uint64_t* world_generation) {
    if (world_generation == nullptr) return ELISA_PHYSICS_INVALID_ARGUMENT;
    const int32_t owner_status = require_application_owner();
    if (owner_status != ELISA_PHYSICS_OK) return owner_status;
    PhysicsService& state = physics_service();
    if (state.initialized) return ELISA_PHYSICS_INVALID_STATE;
    if (state.world_generation == UINT64_MAX) return ELISA_PHYSICS_CAPACITY;
    try {
        state.scene = std::make_unique<wi::scene::Scene>();
    } catch (...) {
        return ELISA_PHYSICS_BACKEND_FAILURE;
    }
#if defined(ELISA_PHYSICS_TEST_PROBE)
    if (state.fail_next_initialize_after_scene) {
        state.fail_next_initialize_after_scene = false;
        state.scene.reset();
        return ELISA_PHYSICS_BACKEND_FAILURE;
    }
#endif
    ++state.world_generation;
    state.tick = 0;
    state.simulation_before = wi::physics::IsSimulationEnabled();
    state.interpolation_before = wi::physics::IsInterpolationEnabled();
    wi::physics::SetSimulationEnabled(true);
    wi::physics::SetInterpolationEnabled(false);
    state.initialized = true;
    *world_generation = state.world_generation;
    return ELISA_PHYSICS_OK;
}

#if defined(ELISA_PHYSICS_TEST_PROBE)
extern "C" int32_t elisa_physics_v1_test_fail_next_initialize_after_scene(void) {
    const int32_t owner_status = require_application_owner();
    if (owner_status != ELISA_PHYSICS_OK) return owner_status;
    PhysicsService& state = physics_service();
    if (state.initialized || state.scene != nullptr || state.fail_next_initialize_after_scene) {
        return ELISA_PHYSICS_INVALID_STATE;
    }
    state.fail_next_initialize_after_scene = true;
    return ELISA_PHYSICS_OK;
}

extern "C" int32_t elisa_physics_v1_test_is_clean(void) {
    const int32_t owner_status = require_application_owner();
    if (owner_status != ELISA_PHYSICS_OK) return owner_status;
    const PhysicsService& state = physics_service();
    return !state.initialized && state.scene == nullptr && !state.fail_next_initialize_after_scene
        ? 1 : 0;
}
#endif

extern "C" int32_t elisa_physics_v1_create_box(uint64_t world_generation, int32_t kind,
    float position_x, float position_y, float position_z,
    float half_x, float half_y, float half_z, float mass,
    uint32_t* slot, uint64_t* body_generation) {
    if (slot == nullptr || body_generation == nullptr ||
        kind < ELISA_PHYSICS_BODY_STATIC || kind > ELISA_PHYSICS_BODY_DYNAMIC ||
        !std::isfinite(position_x) || !std::isfinite(position_y) || !std::isfinite(position_z) ||
        std::fabs(position_x) > MAX_POSITION || std::fabs(position_y) > MAX_POSITION ||
        std::fabs(position_z) > MAX_POSITION || !std::isfinite(half_x) ||
        !std::isfinite(half_y) || !std::isfinite(half_z) || half_x <= 0.0f ||
        half_y <= 0.0f || half_z <= 0.0f || half_x > MAX_HALF_EXTENT ||
        half_y > MAX_HALF_EXTENT || half_z > MAX_HALF_EXTENT || !std::isfinite(mass) ||
        mass < 0.0f || mass > MAX_MASS ||
        (kind == ELISA_PHYSICS_BODY_DYNAMIC && mass <= 0.0f)) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    const int32_t status = require_world(world_generation);
    if (status != ELISA_PHYSICS_OK) return status;
    PhysicsService& state = physics_service();
    uint32_t free_slot = MAX_BODIES;
    for (uint32_t index = 0; index < MAX_BODIES; ++index) {
        if (!state.bodies[index].live) { free_slot = index; break; }
    }
    if (free_slot == MAX_BODIES) return ELISA_PHYSICS_CAPACITY;
    BodySlot& body_slot = state.bodies[free_slot];
    if (body_slot.generation == UINT64_MAX) return ELISA_PHYSICS_CAPACITY;

    wi::ecs::Entity entity = wi::ecs::INVALID_ENTITY;
    try {
        entity = state.scene->Entity_CreateTransform("elisa_physics_body_" + std::to_string(free_slot));
        if (entity == wi::ecs::INVALID_ENTITY) return ELISA_PHYSICS_BACKEND_FAILURE;
        wi::scene::TransformComponent* transform = state.scene->transforms.GetComponent(entity);
        if (transform == nullptr) {
            state.scene->Entity_Remove(entity);
            return ELISA_PHYSICS_BACKEND_FAILURE;
        }
        const ElisaCoordinateProfile profile = elisa_coordinate_profile();
        const ElisaTransformPayload authored{{position_x, position_y, position_z},
            {0.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}};
        if (!probe::submit_elisa_transform(&profile, &authored, transform)) {
            state.scene->Entity_Remove(entity);
            return ELISA_PHYSICS_INVALID_ARGUMENT;
        }
        wi::scene::RigidBodyPhysicsComponent& rigidbody = state.scene->rigidbodies.Create(entity);
        rigidbody.shape = wi::scene::RigidBodyPhysicsComponent::BOX;
        rigidbody.mass = kind == ELISA_PHYSICS_BODY_STATIC ? 0.0f : mass;
        rigidbody.box.halfextents = XMFLOAT3(half_x, half_y, half_z);
        rigidbody.SetKinematic(kind == ELISA_PHYSICS_BODY_KINEMATIC);
        body_slot.entity = entity;
        ++body_slot.generation;
        body_slot.live = true;
    } catch (...) {
        if (entity != wi::ecs::INVALID_ENTITY) state.scene->Entity_Remove(entity);
        return ELISA_PHYSICS_BACKEND_FAILURE;
    }
    *slot = free_slot;
    *body_generation = body_slot.generation;
    return ELISA_PHYSICS_OK;
}

extern "C" int32_t elisa_physics_v1_fixed_step(uint64_t world_generation,
    float delta_seconds, uint64_t* tick) {
    if (tick == nullptr || !std::isfinite(delta_seconds) || delta_seconds <= 0.0f ||
        delta_seconds > MAX_FIXED_DELTA) return ELISA_PHYSICS_INVALID_ARGUMENT;
    const int32_t status = require_world(world_generation);
    if (status != ELISA_PHYSICS_OK) return status;
    PhysicsService& state = physics_service();
    if (state.tick == UINT64_MAX) return ELISA_PHYSICS_CAPACITY;
    try {
        state.scene->Update(delta_seconds);
    } catch (...) {
        return ELISA_PHYSICS_BACKEND_FAILURE;
    }
    *tick = ++state.tick;
    return ELISA_PHYSICS_OK;
}

extern "C" int32_t elisa_physics_v1_body_position(uint64_t world_generation,
    uint32_t slot, uint64_t body_generation, float* x, float* y, float* z) {
    if (x == nullptr || y == nullptr || z == nullptr) return ELISA_PHYSICS_INVALID_ARGUMENT;
    const int32_t status = require_world(world_generation);
    if (status != ELISA_PHYSICS_OK) return status;
    PhysicsService& state = physics_service();
    BodySlot* body = resolve_body(state, slot, body_generation);
    if (body == nullptr) return ELISA_PHYSICS_INVALID_HANDLE;
    const wi::scene::TransformComponent* transform = state.scene->transforms.GetComponent(body->entity);
    ElisaTransformPayload pose{};
    const ElisaCoordinateProfile profile = elisa_coordinate_profile();
    if (!probe::read_elisa_transform(&profile, transform, &pose)) return ELISA_PHYSICS_BACKEND_FAILURE;
    *x = pose.position[0];
    *y = pose.position[1];
    *z = pose.position[2];
    return ELISA_PHYSICS_OK;
}

extern "C" int32_t elisa_physics_v1_destroy_body(uint64_t world_generation,
    uint32_t slot, uint64_t body_generation) {
    const int32_t status = require_world(world_generation);
    if (status != ELISA_PHYSICS_OK) return status;
    PhysicsService& state = physics_service();
    BodySlot* body = resolve_body(state, slot, body_generation);
    if (body == nullptr) return ELISA_PHYSICS_INVALID_HANDLE;
    state.scene->Entity_Remove(body->entity);
    body->entity = wi::ecs::INVALID_ENTITY;
    body->live = false;
    return ELISA_PHYSICS_OK;
}

extern "C" int32_t elisa_physics_v1_shutdown(uint64_t world_generation) {
    const int32_t owner_status = require_application_owner();
    if (owner_status != ELISA_PHYSICS_OK) return owner_status;
    PhysicsService& state = physics_service();
    if (!state.initialized) return ELISA_PHYSICS_INVALID_STATE;
    if (world_generation != state.world_generation) return ELISA_PHYSICS_STALE_WORLD;
    shutdown_world();
    return ELISA_PHYSICS_OK;
}

extern "C" void elisa_physics_v1_shutdown_from_application(void) {
    shutdown_world();
}
