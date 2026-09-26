extern "C" int32_t elisa_physics_v1_raycast_filtered(uint64_t world_generation,
    float origin_x, float origin_y, float origin_z,
    float direction_x, float direction_y, float direction_z,
    float max_distance, uint32_t layer_mask, uint32_t query_targets,
    ElisaPhysicsRayHit* output, int32_t* hit) {
    if (output == nullptr || hit == nullptr ||
        (query_targets & ~uint32_t(ELISA_PHYSICS_QUERY_ALL)) != 0) {
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
        !elisa_physics_coordinates::to_wicked_length(&profile, max_distance,
            backend_max_distance)) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    uint32_t scene_filter_mask = 0;
    if ((query_targets & ELISA_PHYSICS_QUERY_SCENE_OBJECTS) != 0) {
        scene_filter_mask |= wi::enums::FILTER_OBJECT_ALL;
    }
    if ((query_targets & ELISA_PHYSICS_QUERY_SCENE_COLLIDERS) != 0) {
        scene_filter_mask |= wi::enums::FILTER_COLLIDER;
    }
    const bool include_physics_bodies =
        (query_targets & ELISA_PHYSICS_QUERY_PHYSICS_BODIES) != 0;
    probe::PhysicsQueryHit source{};
    const bool found = state.query_bridge != nullptr &&
        state.query_bridge->raycast_filtered(query_token, origin, direction,
            backend_max_distance, layer_mask, scene_filter_mask,
            include_physics_bodies, source);
    *hit = found ? 1 : 0;
    *output = {};
    if (!found) return ELISA_PHYSICS_OK;
    XMFLOAT3 authored_position{};
    XMFLOAT3 authored_normal{};
    float authored_distance = 0.0f;
    if (!elisa_physics_coordinates::from_wicked_position(&profile,
            source.position, authored_position) ||
        !elisa_physics_coordinates::from_wicked_direction(&profile,
            source.normal, authored_normal) ||
        !elisa_physics_coordinates::from_wicked_length(&profile,
            source.distance, authored_distance)) return ELISA_PHYSICS_BACKEND_FAILURE;
    *output = {static_cast<uint64_t>(source.entity), authored_position.x,
        authored_position.y, authored_position.z, authored_normal.x,
        authored_normal.y, authored_normal.z, authored_distance};
    return ELISA_PHYSICS_OK;
}
