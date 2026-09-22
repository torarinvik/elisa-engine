#include "physics_service_abi.h"

#include "application_abi.h"
#include "coordinate_transform_bridge.h"
#include "physics_query_bridge.h"
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
constexpr size_t MAX_CONTACT_EVENTS = ELISA_PHYSICS_MAX_CONTACT_EVENTS;

struct BodySlot {
    wi::ecs::Entity entity = wi::ecs::INVALID_ENTITY;
    uint64_t generation = 0;
    bool live = false;
};

struct PhysicsService {
    std::unique_ptr<wi::scene::Scene> scene;
    std::unique_ptr<probe::PhysicsContactQueueListener> contact_listener;
    std::array<BodySlot, MAX_BODIES> bodies{};
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
    if (state.scene != nullptr) {
        wi::physics::SetContactEventListener(*state.scene, nullptr);
    }
    state.contact_listener.reset();
    state.scene.reset();
    state.contact_queue.reset();
    state.pending_contact_count = 0;
    state.pending_contact_dropped = 0;
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
    if (!state.contact_queue.reset()) return ELISA_PHYSICS_INVALID_STATE;
    try {
        state.scene = std::make_unique<wi::scene::Scene>();
        state.contact_listener = std::make_unique<probe::PhysicsContactQueueListener>(
            state.contact_queue);
        wi::physics::SetContactEventListener(*state.scene, state.contact_listener.get());
    } catch (...) {
        state.contact_listener.reset();
        state.scene.reset();
        return ELISA_PHYSICS_BACKEND_FAILURE;
    }
#if defined(ELISA_PHYSICS_TEST_PROBE)
    if (state.fail_next_initialize_after_scene) {
        state.fail_next_initialize_after_scene = false;
        wi::physics::SetContactEventListener(*state.scene, nullptr);
        state.contact_listener.reset();
        state.scene.reset();
        return ELISA_PHYSICS_BACKEND_FAILURE;
    }
#endif
    ++state.world_generation;
    state.tick = 0;
    state.pending_contact_count = 0;
    state.pending_contact_dropped = 0;
    state.simulation_before = wi::physics::IsSimulationEnabled();
    state.interpolation_before = wi::physics::IsInterpolationEnabled();
    wi::physics::SetSimulationEnabled(true);
    wi::physics::SetInterpolationEnabled(false);
    state.initialized = true;
    *world_generation = state.world_generation;
    return ELISA_PHYSICS_OK;
}

extern "C" int32_t elisa_physics_v1_probe_provider(void) {
    uint64_t world_generation = 0;
    const int32_t initialized = elisa_physics_v1_initialize(&world_generation);
    if (initialized != ELISA_PHYSICS_OK) return initialized;
    return elisa_physics_v1_shutdown(world_generation);
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
    const uint64_t next_tick = state.tick + 1;
    if (!state.contact_queue.begin_step(next_tick)) return ELISA_PHYSICS_INVALID_STATE;
    try {
        state.scene->Update(delta_seconds);
    } catch (...) {
        state.contact_queue.end_step();
        return ELISA_PHYSICS_BACKEND_FAILURE;
    }
    state.contact_queue.drain([&](const probe::PhysicsContactEvent& event) {
        if (state.pending_contact_count < MAX_CONTACT_EVENTS) {
            state.pending_contacts[state.pending_contact_count++] = event;
        } else {
            ++state.pending_contact_dropped;
        }
    });
    state.pending_contact_dropped += state.contact_queue.dropped();
    if (!state.contact_queue.end_step()) return ELISA_PHYSICS_BACKEND_FAILURE;
    *tick = ++state.tick;
    return ELISA_PHYSICS_OK;
}

extern "C" int32_t elisa_physics_v1_poll_contacts(uint64_t world_generation,
    ElisaPhysicsContactEvent* events, uint32_t capacity, uint32_t* count,
    uint32_t* dropped) {
    if (count == nullptr || dropped == nullptr || capacity > ELISA_PHYSICS_MAX_CONTACT_EVENTS ||
        (capacity != 0 && events == nullptr)) return ELISA_PHYSICS_INVALID_ARGUMENT;
    const int32_t status = require_world(world_generation);
    if (status != ELISA_PHYSICS_OK) return status;
    PhysicsService& state = physics_service();
    if (state.pending_contact_count > capacity) {
        *count = static_cast<uint32_t>(state.pending_contact_count);
        *dropped = static_cast<uint32_t>(state.pending_contact_dropped);
        return ELISA_PHYSICS_CAPACITY;
    }
    for (size_t index = 0; index < state.pending_contact_count; ++index) {
        const probe::PhysicsContactEvent& source = state.pending_contacts[index];
        ElisaPhysicsContactEvent& destination = events[index];
        destination.entity_a = source.entity_a;
        destination.entity_b = source.entity_b;
        destination.position_x = source.position.x;
        destination.position_y = source.position.y;
        destination.position_z = source.position.z;
        destination.normal_x = source.normal.x;
        destination.normal_y = source.normal.y;
        destination.normal_z = source.normal.z;
        destination.penetration_depth = source.depth;
        destination.kind = static_cast<int32_t>(source.kind);
        destination.trigger = source.trigger ? 1 : 0;
        destination.sequence = source.sequence;
    }
    *count = static_cast<uint32_t>(state.pending_contact_count);
    *dropped = static_cast<uint32_t>(state.pending_contact_dropped);
    state.pending_contact_count = 0;
    state.pending_contact_dropped = 0;
    return ELISA_PHYSICS_OK;
}

extern "C" int32_t elisa_physics_v1_contact_count(uint64_t world_generation,
    uint32_t* count, uint32_t* dropped) {
    if (count == nullptr || dropped == nullptr) return ELISA_PHYSICS_INVALID_ARGUMENT;
    const int32_t status = require_world(world_generation);
    if (status != ELISA_PHYSICS_OK) return status;
    const PhysicsService& state = physics_service();
    *count = static_cast<uint32_t>(state.pending_contact_count);
    *dropped = static_cast<uint32_t>(state.pending_contact_dropped);
    return ELISA_PHYSICS_OK;
}

extern "C" int32_t elisa_physics_v1_contact_at(uint64_t world_generation, uint32_t index,
    uint64_t* entity_a, uint64_t* entity_b,
    float* position_x, float* position_y, float* position_z,
    float* normal_x, float* normal_y, float* normal_z,
    float* penetration_depth, int32_t* kind, int32_t* trigger, uint64_t* sequence) {
    if (entity_a == nullptr || entity_b == nullptr || position_x == nullptr ||
        position_y == nullptr || position_z == nullptr || normal_x == nullptr ||
        normal_y == nullptr || normal_z == nullptr || penetration_depth == nullptr ||
        kind == nullptr || trigger == nullptr || sequence == nullptr) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    const int32_t status = require_world(world_generation);
    if (status != ELISA_PHYSICS_OK) return status;
    const PhysicsService& state = physics_service();
    if (index >= state.pending_contact_count) return ELISA_PHYSICS_INVALID_ARGUMENT;
    const probe::PhysicsContactEvent& source = state.pending_contacts[index];
    *entity_a = source.entity_a;
    *entity_b = source.entity_b;
    *position_x = source.position.x;
    *position_y = source.position.y;
    *position_z = source.position.z;
    *normal_x = source.normal.x;
    *normal_y = source.normal.y;
    *normal_z = source.normal.z;
    *penetration_depth = source.depth;
    *kind = static_cast<int32_t>(source.kind);
    *trigger = source.trigger ? 1 : 0;
    *sequence = source.sequence;
    return ELISA_PHYSICS_OK;
}

extern "C" int32_t elisa_physics_v1_clear_contacts(uint64_t world_generation) {
    const int32_t status = require_world(world_generation);
    if (status != ELISA_PHYSICS_OK) return status;
    PhysicsService& state = physics_service();
    state.pending_contact_count = 0;
    state.pending_contact_dropped = 0;
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

extern "C" int32_t elisa_physics_v1_set_kinematic_target(uint64_t world_generation,
    uint32_t slot, uint64_t body_generation,
    float position_x, float position_y, float position_z,
    float rotation_x, float rotation_y, float rotation_z, float rotation_w) {
    if (!std::isfinite(position_x) || !std::isfinite(position_y) || !std::isfinite(position_z) ||
        std::fabs(position_x) > MAX_POSITION || std::fabs(position_y) > MAX_POSITION ||
        std::fabs(position_z) > MAX_POSITION || !std::isfinite(rotation_x) ||
        !std::isfinite(rotation_y) || !std::isfinite(rotation_z) || !std::isfinite(rotation_w)) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    const double rotation_length = std::sqrt(double(rotation_x) * rotation_x +
        double(rotation_y) * rotation_y + double(rotation_z) * rotation_z + double(rotation_w) * rotation_w);
    if (!std::isfinite(rotation_length) || rotation_length <= 1.0e-6) return ELISA_PHYSICS_INVALID_ARGUMENT;
    const int32_t status = require_world(world_generation);
    if (status != ELISA_PHYSICS_OK) return status;
    PhysicsService& state = physics_service();
    BodySlot* body = resolve_body(state, slot, body_generation);
    if (body == nullptr) return ELISA_PHYSICS_INVALID_HANDLE;
    wi::scene::RigidBodyPhysicsComponent* rigidbody = state.scene->rigidbodies.GetComponent(body->entity);
    wi::scene::TransformComponent* transform = state.scene->transforms.GetComponent(body->entity);
    if (rigidbody == nullptr || transform == nullptr || !rigidbody->IsKinematic()) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    const ElisaCoordinateProfile profile = elisa_coordinate_profile();
    const ElisaTransformPayload authored{{position_x, position_y, position_z},
        {rotation_x, rotation_y, rotation_z, rotation_w}, {1.0f, 1.0f, 1.0f}};
    return probe::submit_elisa_transform(&profile, &authored, transform)
        ? ELISA_PHYSICS_OK : ELISA_PHYSICS_BACKEND_FAILURE;
}

extern "C" int32_t elisa_physics_v1_set_sleeping(uint64_t world_generation,
    uint32_t slot, uint64_t body_generation, int32_t sleeping) {
    if (sleeping != 0 && sleeping != 1) return ELISA_PHYSICS_INVALID_ARGUMENT;
    const int32_t status = require_world(world_generation);
    if (status != ELISA_PHYSICS_OK) return status;
    PhysicsService& state = physics_service();
    BodySlot* body = resolve_body(state, slot, body_generation);
    if (body == nullptr) return ELISA_PHYSICS_INVALID_HANDLE;
    wi::scene::RigidBodyPhysicsComponent* rigidbody = state.scene->rigidbodies.GetComponent(body->entity);
    if (rigidbody == nullptr || rigidbody->physicsobject == nullptr || rigidbody->mass <= 0.0f ||
        rigidbody->IsKinematic()) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    try {
        wi::physics::SetActivationState(*rigidbody,
            sleeping != 0 ? wi::physics::ActivationState::Inactive
                          : wi::physics::ActivationState::Active);
    } catch (...) {
        return ELISA_PHYSICS_BACKEND_FAILURE;
    }
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
