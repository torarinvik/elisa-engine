#include "physics_service_abi.h"

#include "coordinate_transform_bridge.h"
#include "coordinate_conventions.h"
#include "cooked_geometry_package.h"
#include "physics_coordinate_bridge.h"
#include "physics_service_internal.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <filesystem>
#include <vector>

namespace {

constexpr float MAX_POSITION = 1.0e6f;
constexpr float MAX_HALF_EXTENT = 1.0e4f;
constexpr float MAX_CENTER_OF_MASS_OFFSET = 1.0e4f;
constexpr float MAX_MASS = 1.0e8f;
constexpr float MAX_FIXED_DELTA = 1.0f / 30.0f;
using namespace elisa_physics_internal;

static_assert(sizeof(ElisaPhysicsContactEvent) == 64,
    "Elisa contact event ABI must match the fixed Elisa ContactEvent layout");
static_assert(offsetof(ElisaPhysicsContactEvent, sequence) == 56,
    "Elisa contact event sequence offset changed");
static_assert(sizeof(ElisaPhysicsRayHit) == 40,
    "Elisa ray hit ABI must remain a flat fixed-width record");
static_assert(offsetof(ElisaPhysicsRayHit, distance) == 32,
    "Elisa ray hit distance offset changed");
static_assert(offsetof(ElisaPhysicsRayHitBuffer, count) == 640,
    "Elisa ray hit buffer count offset changed");
void shutdown_world() {
    PhysicsService& state = physics_service();
    if (!state.initialized) return;
    if (state.scene != nullptr) {
        wi::physics::SetContactEventListener(*state.scene, nullptr);
    }
    state.query_bridge.reset();
    state.contact_listener.reset();
    state.scene.reset();
    state.contact_queue.reset();
    state.pending_contact_count = 0;
    state.pending_contact_dropped = 0;
    for (BodySlot& body : state.bodies) {
        body.entity = wi::ecs::INVALID_ENTITY;
        body.shape_slot = INVALID_SHAPE_SLOT;
        body.shape_generation = 0;
        body.mesh_proxy_geometry_bytes = 0;
        body.live = false;
    }
    for (ShapeSlot& shape : state.shapes) {
        shape.backend_shape = wi::scene::RigidBodyPhysicsComponent{};
        std::vector<XMFLOAT3>().swap(shape.mesh_vertices);
        std::vector<uint32_t>().swap(shape.mesh_indices);
        shape.mesh_geometry_bytes = 0;
        shape.body_references = 0;
        shape.compound_references = 0;
        std::vector<elisa_physics_internal::ShapeReference>().swap(shape.child_shapes);
        shape.requires_static_body = false;
        shape.live = false;
    }
    state.mesh_geometry_bytes = 0;
    state.mesh_proxy_geometry_bytes = 0;
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
        state.query_bridge = std::make_unique<probe::PhysicsQueryBridge>(*state.scene);
        state.contact_listener = std::make_unique<probe::PhysicsContactQueueListener>(
            state.contact_queue);
        wi::physics::SetContactEventListener(*state.scene, state.contact_listener.get());
    } catch (...) {
        state.contact_listener.reset();
        state.query_bridge.reset();
        state.scene.reset();
        return ELISA_PHYSICS_BACKEND_FAILURE;
    }
#if defined(ELISA_PHYSICS_TEST_PROBE)
    if (state.fail_next_initialize_after_scene) {
        state.fail_next_initialize_after_scene = false;
        wi::physics::SetContactEventListener(*state.scene, nullptr);
        state.contact_listener.reset();
        state.query_bridge.reset();
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

extern "C" int32_t elisa_physics_v1_set_layer_collision(uint64_t world_generation,
    uint32_t layer_a, uint32_t layer_b, int32_t enabled) {
    if (layer_a >= wi::physics::COLLISION_LAYER_COUNT ||
        layer_b >= wi::physics::COLLISION_LAYER_COUNT ||
        (enabled != 0 && enabled != 1)) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    const int32_t status = require_world(world_generation);
    if (status != ELISA_PHYSICS_OK) return status;
    PhysicsService& state = physics_service();
    for (const BodySlot& body : state.bodies) {
        if (body.live) return ELISA_PHYSICS_CONFIGURATION_LOCKED;
    }
    return wi::physics::SetCollisionLayerCollision(*state.scene, layer_a, layer_b,
        enabled != 0) ? ELISA_PHYSICS_OK : ELISA_PHYSICS_INVALID_ARGUMENT;
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

#include "physics_create_body_abi.inc"
#include "physics_reusable_shape_abi.inc"
#include "physics_create_body_with_shape_abi.inc"
#include "physics_compound_shape_abi.inc"
#include "physics_body_motion_abi.inc"
#include "physics_body_material_abi.inc"
#include "physics_body_inertia_abi.inc"

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

extern "C" int32_t elisa_physics_v1_raycast(uint64_t world_generation,
    float origin_x, float origin_y, float origin_z,
    float direction_x, float direction_y, float direction_z,
    float max_distance, uint32_t layer_mask,
    uint64_t* entity, float* position_x, float* position_y, float* position_z,
    float* normal_x, float* normal_y, float* normal_z, float* distance,
    int32_t* hit) {
    if (entity == nullptr || position_x == nullptr || position_y == nullptr ||
        position_z == nullptr || normal_x == nullptr || normal_y == nullptr ||
        normal_z == nullptr || distance == nullptr || hit == nullptr) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    const float direction_length_squared = direction_x * direction_x +
        direction_y * direction_y + direction_z * direction_z;
    if (!std::isfinite(origin_x) || !std::isfinite(origin_y) ||
        !std::isfinite(origin_z) || !std::isfinite(direction_x) ||
        !std::isfinite(direction_y) || !std::isfinite(direction_z) ||
        !std::isfinite(max_distance) || max_distance <= 0.0f ||
        !std::isfinite(direction_length_squared) ||
        direction_length_squared <= 0.000001f) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    const int32_t status = require_world(world_generation);
    if (status != ELISA_PHYSICS_OK) return status;
    PhysicsService& state = physics_service();
    probe::PhysicsQueryHit result{};
    const probe::PhysicsQueryToken query_token = state.query_bridge != nullptr
        ? state.query_bridge->acquire() : probe::PhysicsQueryToken{};
    const ElisaCoordinateProfile profile = elisa_coordinate_profile();
    XMFLOAT3 origin{};
    XMFLOAT3 direction{};
    float backend_max_distance = 0.0f;
    if (!elisa_physics_coordinates::to_wicked_position(&profile,
            XMFLOAT3(origin_x, origin_y, origin_z), origin) ||
        !elisa_physics_coordinates::to_wicked_direction(&profile,
            XMFLOAT3(direction_x, direction_y, direction_z), direction) ||
        !elisa_physics_coordinates::to_wicked_length(&profile, max_distance, backend_max_distance)) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    const bool found = state.query_bridge != nullptr &&
        state.query_bridge->raycast_physics(query_token, origin, direction,
            backend_max_distance, layer_mask, result);
    XMFLOAT3 authored_position{};
    XMFLOAT3 authored_normal{};
    float authored_distance = 0.0f;
    if (found && (!elisa_physics_coordinates::from_wicked_position(&profile,
            result.position, authored_position) ||
        !elisa_physics_coordinates::from_wicked_direction(&profile,
            result.normal, authored_normal) ||
        !elisa_physics_coordinates::from_wicked_length(&profile,
            result.distance, authored_distance))) return ELISA_PHYSICS_BACKEND_FAILURE;
    *entity = found ? static_cast<uint64_t>(result.entity) : 0;
    *position_x = found ? authored_position.x : 0.0f;
    *position_y = found ? authored_position.y : 0.0f;
    *position_z = found ? authored_position.z : 0.0f;
    *normal_x = found ? authored_normal.x : 0.0f;
    *normal_y = found ? authored_normal.y : 0.0f;
    *normal_z = found ? authored_normal.z : 0.0f;
    *distance = found ? authored_distance : 0.0f;
    *hit = found ? 1 : 0;
    return ELISA_PHYSICS_OK;
}

extern "C" int32_t elisa_physics_v1_raycast_all(uint64_t world_generation,
    float origin_x, float origin_y, float origin_z,
    float direction_x, float direction_y, float direction_z,
    float max_distance, uint32_t layer_mask,
    ElisaPhysicsRayHitBuffer* buffer) {
    if (buffer == nullptr) return ELISA_PHYSICS_INVALID_ARGUMENT;
    const float direction_length_squared = direction_x * direction_x +
        direction_y * direction_y + direction_z * direction_z;
    if (!std::isfinite(origin_x) || !std::isfinite(origin_y) ||
        !std::isfinite(origin_z) || !std::isfinite(direction_x) ||
        !std::isfinite(direction_y) || !std::isfinite(direction_z) ||
        !std::isfinite(max_distance) || max_distance <= 0.0f ||
        !std::isfinite(direction_length_squared) ||
        direction_length_squared <= 0.000001f) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    const int32_t status = require_world(world_generation);
    if (status != ELISA_PHYSICS_OK) return status;
    PhysicsService& state = physics_service();
    probe::PhysicsQueryBridge::Hits results{};
    const probe::PhysicsQueryToken query_token = state.query_bridge != nullptr
        ? state.query_bridge->acquire() : probe::PhysicsQueryToken{};
    const ElisaCoordinateProfile profile = elisa_coordinate_profile();
    XMFLOAT3 origin{};
    XMFLOAT3 direction{};
    float backend_max_distance = 0.0f;
    if (!elisa_physics_coordinates::to_wicked_position(&profile,
            XMFLOAT3(origin_x, origin_y, origin_z), origin) ||
        !elisa_physics_coordinates::to_wicked_direction(&profile,
            XMFLOAT3(direction_x, direction_y, direction_z), direction) ||
        !elisa_physics_coordinates::to_wicked_length(&profile, max_distance, backend_max_distance)) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    const size_t found = state.query_bridge != nullptr
        ? state.query_bridge->raycast_all(query_token,
            origin, direction, backend_max_distance, layer_mask, results)
        : 0;
    buffer->count = static_cast<uint32_t>(found);
    for (size_t index = 0; index < found; ++index) {
        const probe::PhysicsQueryHit& source = results.values[index];
        ElisaPhysicsRayHit& destination = buffer->hits[index];
        XMFLOAT3 authored_position{};
        XMFLOAT3 authored_normal{};
        float authored_distance = 0.0f;
        if (!elisa_physics_coordinates::from_wicked_position(&profile,
                source.position, authored_position) ||
            !elisa_physics_coordinates::from_wicked_direction(&profile,
                source.normal, authored_normal) ||
            !elisa_physics_coordinates::from_wicked_length(&profile,
                source.distance, authored_distance)) return ELISA_PHYSICS_BACKEND_FAILURE;
        destination.entity = static_cast<uint64_t>(source.entity);
        destination.position_x = authored_position.x;
        destination.position_y = authored_position.y;
        destination.position_z = authored_position.z;
        destination.normal_x = authored_normal.x;
        destination.normal_y = authored_normal.y;
        destination.normal_z = authored_normal.z;
        destination.distance = authored_distance;
    }
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
    const ElisaCoordinateProfile profile = elisa_coordinate_profile();
    for (size_t index = 0; index < state.pending_contact_count; ++index) {
        const probe::PhysicsContactEvent& source = state.pending_contacts[index];
        ElisaPhysicsContactEvent& destination = events[index];
        XMFLOAT3 authored_position{};
        XMFLOAT3 authored_normal{};
        float authored_depth = 0.0f;
        if (!elisa_physics_coordinates::from_wicked_position(&profile,
                source.position, authored_position) ||
            !elisa_physics_coordinates::from_wicked_direction(&profile,
                source.normal, authored_normal) ||
            !elisa_physics_coordinates::from_wicked_length(&profile,
                source.depth, authored_depth)) return ELISA_PHYSICS_BACKEND_FAILURE;
        destination.entity_a = source.entity_a;
        destination.entity_b = source.entity_b;
        destination.position_x = authored_position.x;
        destination.position_y = authored_position.y;
        destination.position_z = authored_position.z;
        destination.normal_x = authored_normal.x;
        destination.normal_y = authored_normal.y;
        destination.normal_z = authored_normal.z;
        destination.penetration_depth = authored_depth;
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
    const ElisaCoordinateProfile profile = elisa_coordinate_profile();
    XMFLOAT3 authored_position{};
    XMFLOAT3 authored_normal{};
    float authored_depth = 0.0f;
    if (!elisa_physics_coordinates::from_wicked_position(&profile,
            source.position, authored_position) ||
        !elisa_physics_coordinates::from_wicked_direction(&profile,
            source.normal, authored_normal) ||
        !elisa_physics_coordinates::from_wicked_length(&profile,
            source.depth, authored_depth)) return ELISA_PHYSICS_BACKEND_FAILURE;
    *entity_a = source.entity_a;
    *entity_b = source.entity_b;
    *position_x = authored_position.x;
    *position_y = authored_position.y;
    *position_z = authored_position.z;
    *normal_x = authored_normal.x;
    *normal_y = authored_normal.y;
    *normal_z = authored_normal.z;
    *penetration_depth = authored_depth;
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

extern "C" int32_t elisa_physics_v1_body_pose(uint64_t world_generation,
    uint32_t slot, uint64_t body_generation, float* position_x, float* position_y,
    float* position_z, float* rotation_x, float* rotation_y, float* rotation_z,
    float* rotation_w) {
    if (position_x == nullptr || position_y == nullptr || position_z == nullptr ||
        rotation_x == nullptr || rotation_y == nullptr || rotation_z == nullptr ||
        rotation_w == nullptr) return ELISA_PHYSICS_INVALID_ARGUMENT;
    const int32_t status = require_world(world_generation);
    if (status != ELISA_PHYSICS_OK) return status;
    PhysicsService& state = physics_service();
    BodySlot* body = resolve_body(state, slot, body_generation);
    if (body == nullptr) return ELISA_PHYSICS_INVALID_HANDLE;
    const wi::scene::TransformComponent* transform =
        state.scene->transforms.GetComponent(body->entity);
    ElisaTransformPayload pose{};
    const ElisaCoordinateProfile profile = elisa_coordinate_profile();
    if (!probe::read_elisa_transform(&profile, transform, &pose)) {
        return ELISA_PHYSICS_BACKEND_FAILURE;
    }
    *position_x = pose.position[0];
    *position_y = pose.position[1];
    *position_z = pose.position[2];
    *rotation_x = pose.rotation_xyzw[0];
    *rotation_y = pose.rotation_xyzw[1];
    *rotation_z = pose.rotation_xyzw[2];
    *rotation_w = pose.rotation_xyzw[3];
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
    ShapeSlot* referenced_shape = nullptr;
    if (body->shape_slot != INVALID_SHAPE_SLOT) {
        if (body->shape_slot >= MAX_SHAPES) return ELISA_PHYSICS_BACKEND_FAILURE;
        ShapeSlot& shape = state.shapes[body->shape_slot];
        if (!shape.live || shape.generation != body->shape_generation || shape.body_references == 0) {
            return ELISA_PHYSICS_BACKEND_FAILURE;
        }
        referenced_shape = &shape;
    }
    if (body->mesh_proxy_geometry_bytes > state.mesh_proxy_geometry_bytes) {
        return ELISA_PHYSICS_BACKEND_FAILURE;
    }
    wi::physics::UnregisterCollisionLayerBody(*state.scene, slot);
    state.scene->Entity_Remove(body->entity);
    body->entity = wi::ecs::INVALID_ENTITY;
    state.mesh_proxy_geometry_bytes -= body->mesh_proxy_geometry_bytes;
    body->mesh_proxy_geometry_bytes = 0;
    if (referenced_shape != nullptr) {
        --referenced_shape->body_references;
        body->shape_slot = INVALID_SHAPE_SLOT;
        body->shape_generation = 0;
    }
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
