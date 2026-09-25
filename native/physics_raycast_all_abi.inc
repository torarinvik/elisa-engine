namespace {
int32_t raycast_all_impl(uint64_t world_generation,
    float origin_x, float origin_y, float origin_z,
    float direction_x, float direction_y, float direction_z,
    float max_distance, uint32_t layer_mask, uint32_t query_targets,
    ElisaPhysicsRayHitBuffer* buffer) {
    if (buffer == nullptr ||
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
    uint32_t scene_filter_mask = 0;
    if ((query_targets & ELISA_PHYSICS_QUERY_SCENE_OBJECTS) != 0) {
        scene_filter_mask |= wi::enums::FILTER_OBJECT_ALL;
    }
    if ((query_targets & ELISA_PHYSICS_QUERY_SCENE_COLLIDERS) != 0) {
        scene_filter_mask |= wi::enums::FILTER_COLLIDER;
    }
    const bool include_physics_bodies =
        (query_targets & ELISA_PHYSICS_QUERY_PHYSICS_BODIES) != 0;
    const size_t found = state.query_bridge != nullptr
        ? state.query_bridge->raycast_all(query_token, origin, direction,
            backend_max_distance, layer_mask, results, scene_filter_mask,
            include_physics_bodies)
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
} // namespace

extern "C" int32_t elisa_physics_v1_raycast_all(uint64_t world_generation,
    float origin_x, float origin_y, float origin_z,
    float direction_x, float direction_y, float direction_z,
    float max_distance, uint32_t layer_mask,
    ElisaPhysicsRayHitBuffer* buffer) {
    return raycast_all_impl(world_generation, origin_x, origin_y, origin_z,
        direction_x, direction_y, direction_z, max_distance, layer_mask,
        ELISA_PHYSICS_QUERY_ALL, buffer);
}

extern "C" int32_t elisa_physics_v1_raycast_all_filtered(uint64_t world_generation,
    float origin_x, float origin_y, float origin_z,
    float direction_x, float direction_y, float direction_z,
    float max_distance, uint32_t layer_mask, uint32_t query_targets,
    ElisaPhysicsRayHitBuffer* buffer) {
    return raycast_all_impl(world_generation, origin_x, origin_y, origin_z,
        direction_x, direction_y, direction_z, max_distance, layer_mask,
        query_targets, buffer);
}

