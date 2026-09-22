#include "physics_service_internal.h"

#include <cmath>
#include <cstddef>

namespace {

using namespace elisa_physics_internal;

static_assert(sizeof(ElisaPhysicsShapeHit) == 40,
    "Elisa shape hit ABI must remain a flat fixed-width record");
static_assert(offsetof(ElisaPhysicsShapeHit, distance) == 32,
    "Elisa shape hit distance offset changed");
static_assert(offsetof(ElisaPhysicsShapeHit, penetration_depth) == 36,
    "Elisa shape hit penetration offset changed");
static_assert(offsetof(ElisaPhysicsShapeHitBuffer, count) == 640,
    "Elisa shape hit buffer count offset changed");

bool finite_vector(float x, float y, float z) {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z);
}

bool valid_cast(float center_x, float center_y, float center_z,
    float direction_x, float direction_y, float direction_z,
    float max_distance, float radius) {
    const float direction_length_squared = direction_x * direction_x +
        direction_y * direction_y + direction_z * direction_z;
    return finite_vector(center_x, center_y, center_z) &&
        finite_vector(direction_x, direction_y, direction_z) &&
        std::isfinite(max_distance) && max_distance > 0.0f &&
        std::isfinite(radius) && radius > 0.0f &&
        std::isfinite(direction_length_squared) && direction_length_squared > 0.000001f;
}

bool valid_overlap(float radius) {
    return std::isfinite(radius) && radius > 0.0f;
}

void copy_shape_hit(const probe::PhysicsQueryHit& source, ElisaPhysicsShapeHit* destination) {
    destination->entity = static_cast<uint64_t>(source.entity);
    destination->position_x = source.position.x;
    destination->position_y = source.position.y;
    destination->position_z = source.position.z;
    destination->normal_x = source.normal.x;
    destination->normal_y = source.normal.y;
    destination->normal_z = source.normal.z;
    destination->distance = source.distance;
    destination->penetration_depth = source.depth;
}

} // namespace

extern "C" int32_t elisa_physics_v1_sphere_cast(uint64_t world_generation,
    float center_x, float center_y, float center_z,
    float direction_x, float direction_y, float direction_z,
    float max_distance, float radius, uint32_t layer_mask,
    ElisaPhysicsShapeHit* result, int32_t* hit) {
    if (result == nullptr || hit == nullptr) return ELISA_PHYSICS_INVALID_ARGUMENT;
    *result = {};
    *hit = 0;
    if (!valid_cast(center_x, center_y, center_z, direction_x, direction_y, direction_z,
            max_distance, radius)) return ELISA_PHYSICS_INVALID_ARGUMENT;
    const int32_t status = elisa_physics_internal::require_world(world_generation);
    if (status != ELISA_PHYSICS_OK) return status;
    elisa_physics_internal::PhysicsService& state = elisa_physics_internal::physics_service();
    probe::PhysicsQueryHit source{};
    const probe::PhysicsQueryToken query_token = state.query_bridge != nullptr
        ? state.query_bridge->acquire() : probe::PhysicsQueryToken{};
    const bool found = state.query_bridge != nullptr && state.query_bridge->sphere_cast(
        query_token, XMFLOAT3(center_x, center_y, center_z),
        XMFLOAT3(direction_x, direction_y, direction_z), max_distance, radius,
        layer_mask, source);
    if (found) copy_shape_hit(source, result);
    *hit = found ? 1 : 0;
    return ELISA_PHYSICS_OK;
}

extern "C" int32_t elisa_physics_v1_capsule_cast(uint64_t world_generation,
    float base_x, float base_y, float base_z,
    float tip_x, float tip_y, float tip_z,
    float direction_x, float direction_y, float direction_z,
    float max_distance, float radius, uint32_t layer_mask,
    ElisaPhysicsShapeHit* result, int32_t* hit) {
    if (result == nullptr || hit == nullptr) return ELISA_PHYSICS_INVALID_ARGUMENT;
    *result = {};
    *hit = 0;
    if (!valid_cast(base_x, base_y, base_z, direction_x, direction_y, direction_z,
            max_distance, radius) || !finite_vector(tip_x, tip_y, tip_z)) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    const int32_t status = elisa_physics_internal::require_world(world_generation);
    if (status != ELISA_PHYSICS_OK) return status;
    elisa_physics_internal::PhysicsService& state = elisa_physics_internal::physics_service();
    probe::PhysicsQueryHit source{};
    const probe::PhysicsQueryToken query_token = state.query_bridge != nullptr
        ? state.query_bridge->acquire() : probe::PhysicsQueryToken{};
    const bool found = state.query_bridge != nullptr && state.query_bridge->capsule_cast(
        query_token, XMFLOAT3(base_x, base_y, base_z), XMFLOAT3(tip_x, tip_y, tip_z),
        XMFLOAT3(direction_x, direction_y, direction_z), max_distance, radius,
        layer_mask, source);
    if (found) copy_shape_hit(source, result);
    *hit = found ? 1 : 0;
    return ELISA_PHYSICS_OK;
}

extern "C" int32_t elisa_physics_v1_overlap_sphere(uint64_t world_generation,
    float center_x, float center_y, float center_z, float radius, uint32_t layer_mask,
    ElisaPhysicsShapeHit* result, int32_t* hit) {
    if (result == nullptr || hit == nullptr) return ELISA_PHYSICS_INVALID_ARGUMENT;
    *result = {};
    *hit = 0;
    if (!finite_vector(center_x, center_y, center_z) || !valid_overlap(radius)) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    const int32_t status = elisa_physics_internal::require_world(world_generation);
    if (status != ELISA_PHYSICS_OK) return status;
    elisa_physics_internal::PhysicsService& state = elisa_physics_internal::physics_service();
    probe::PhysicsQueryHit source{};
    const probe::PhysicsQueryToken query_token = state.query_bridge != nullptr
        ? state.query_bridge->acquire() : probe::PhysicsQueryToken{};
    const bool found = state.query_bridge != nullptr && state.query_bridge->overlap_sphere(
        query_token, XMFLOAT3(center_x, center_y, center_z), radius, layer_mask, source);
    if (found) copy_shape_hit(source, result);
    *hit = found ? 1 : 0;
    return ELISA_PHYSICS_OK;
}

extern "C" int32_t elisa_physics_v1_overlap_sphere_all(uint64_t world_generation,
    float center_x, float center_y, float center_z, float radius, uint32_t layer_mask,
    ElisaPhysicsShapeHitBuffer* buffer) {
    if (buffer == nullptr) return ELISA_PHYSICS_INVALID_ARGUMENT;
    buffer->count = 0;
    if (!finite_vector(center_x, center_y, center_z) || !valid_overlap(radius)) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    const int32_t status = elisa_physics_internal::require_world(world_generation);
    if (status != ELISA_PHYSICS_OK) return status;
    elisa_physics_internal::PhysicsService& state = elisa_physics_internal::physics_service();
    probe::PhysicsQueryBridge::Hits results{};
    const probe::PhysicsQueryToken query_token = state.query_bridge != nullptr
        ? state.query_bridge->acquire() : probe::PhysicsQueryToken{};
    const size_t found = state.query_bridge != nullptr ? state.query_bridge->overlap_sphere_all(
        query_token, XMFLOAT3(center_x, center_y, center_z), radius, layer_mask, results) : 0;
    if (found > ELISA_PHYSICS_MAX_QUERY_HITS) return ELISA_PHYSICS_CAPACITY;
    for (size_t index = 0; index < found; ++index) {
        copy_shape_hit(results.values[index], &buffer->hits[index]);
    }
    buffer->count = static_cast<uint32_t>(found);
    return ELISA_PHYSICS_OK;
}

extern "C" int32_t elisa_physics_v1_overlap_capsule(uint64_t world_generation,
    float base_x, float base_y, float base_z,
    float tip_x, float tip_y, float tip_z, float radius, uint32_t layer_mask,
    ElisaPhysicsShapeHit* result, int32_t* hit) {
    if (result == nullptr || hit == nullptr) return ELISA_PHYSICS_INVALID_ARGUMENT;
    *result = {};
    *hit = 0;
    if (!finite_vector(base_x, base_y, base_z) || !finite_vector(tip_x, tip_y, tip_z) ||
        !valid_overlap(radius)) return ELISA_PHYSICS_INVALID_ARGUMENT;
    const int32_t status = elisa_physics_internal::require_world(world_generation);
    if (status != ELISA_PHYSICS_OK) return status;
    elisa_physics_internal::PhysicsService& state = elisa_physics_internal::physics_service();
    probe::PhysicsQueryHit source{};
    const probe::PhysicsQueryToken query_token = state.query_bridge != nullptr
        ? state.query_bridge->acquire() : probe::PhysicsQueryToken{};
    const bool found = state.query_bridge != nullptr && state.query_bridge->overlap_capsule(
        query_token, XMFLOAT3(base_x, base_y, base_z), XMFLOAT3(tip_x, tip_y, tip_z),
        radius, layer_mask, source);
    if (found) copy_shape_hit(source, result);
    *hit = found ? 1 : 0;
    return ELISA_PHYSICS_OK;
}

extern "C" int32_t elisa_physics_v1_overlap_capsule_all(uint64_t world_generation,
    float base_x, float base_y, float base_z,
    float tip_x, float tip_y, float tip_z, float radius, uint32_t layer_mask,
    ElisaPhysicsShapeHitBuffer* buffer) {
    if (buffer == nullptr) return ELISA_PHYSICS_INVALID_ARGUMENT;
    buffer->count = 0;
    if (!finite_vector(base_x, base_y, base_z) || !finite_vector(tip_x, tip_y, tip_z) ||
        !valid_overlap(radius)) return ELISA_PHYSICS_INVALID_ARGUMENT;
    const int32_t status = elisa_physics_internal::require_world(world_generation);
    if (status != ELISA_PHYSICS_OK) return status;
    elisa_physics_internal::PhysicsService& state = elisa_physics_internal::physics_service();
    probe::PhysicsQueryBridge::Hits results{};
    const probe::PhysicsQueryToken query_token = state.query_bridge != nullptr
        ? state.query_bridge->acquire() : probe::PhysicsQueryToken{};
    const size_t found = state.query_bridge != nullptr ? state.query_bridge->overlap_capsule_all(
        query_token, XMFLOAT3(base_x, base_y, base_z), XMFLOAT3(tip_x, tip_y, tip_z),
        radius, layer_mask, results) : 0;
    if (found > ELISA_PHYSICS_MAX_QUERY_HITS) return ELISA_PHYSICS_CAPACITY;
    for (size_t index = 0; index < found; ++index) {
        copy_shape_hit(results.values[index], &buffer->hits[index]);
    }
    buffer->count = static_cast<uint32_t>(found);
    return ELISA_PHYSICS_OK;
}
