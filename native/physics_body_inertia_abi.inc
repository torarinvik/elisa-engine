namespace {

constexpr float MIN_BODY_PRINCIPAL_MOMENT = 0.000001f;
constexpr float MAX_BODY_PRINCIPAL_MOMENT = 1.0e12f;
constexpr float MIN_BODY_INERTIA_ROTATION_LENGTH_SQUARED = 1.0e-12f;

bool valid_principal_moments(float x, float y, float z) {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) &&
        x >= MIN_BODY_PRINCIPAL_MOMENT && x <= MAX_BODY_PRINCIPAL_MOMENT &&
        y >= MIN_BODY_PRINCIPAL_MOMENT && y <= MAX_BODY_PRINCIPAL_MOMENT &&
        z >= MIN_BODY_PRINCIPAL_MOMENT && z <= MAX_BODY_PRINCIPAL_MOMENT;
}

bool valid_inertia_rotation(float x, float y, float z, float w) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(w)) {
        return false;
    }
    const float length_squared = x * x + y * y + z * z + w * w;
    return std::isfinite(length_squared) &&
        length_squared > MIN_BODY_INERTIA_ROTATION_LENGTH_SQUARED;
}

XMFLOAT4 normalized_wicked_inertia_rotation(float x, float y, float z, float w) {
    XMFLOAT4 rotation = probe::coordinates::to_wicked_rotation(x, y, z, w);
    const float inverse_length = 1.0f / std::sqrt(rotation.x * rotation.x +
        rotation.y * rotation.y + rotation.z * rotation.z + rotation.w * rotation.w);
    return XMFLOAT4(rotation.x * inverse_length, rotation.y * inverse_length,
        rotation.z * inverse_length, rotation.w * inverse_length);
}

} // namespace

extern "C" int32_t elisa_physics_v1_body_set_inertia(uint64_t world_generation,
    uint32_t slot, uint64_t body_generation, float moment_x, float moment_y,
    float moment_z, float rotation_x, float rotation_y, float rotation_z,
    float rotation_w) {
    if (!valid_principal_moments(moment_x, moment_y, moment_z) ||
        !valid_inertia_rotation(rotation_x, rotation_y, rotation_z, rotation_w)) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    const int32_t status = require_world(world_generation);
    if (status != ELISA_PHYSICS_OK) return status;
    PhysicsService& state = physics_service();
    BodySlot* body = resolve_body(state, slot, body_generation);
    if (body == nullptr) return ELISA_PHYSICS_INVALID_HANDLE;
    wi::scene::RigidBodyPhysicsComponent* rigidbody =
        state.scene->rigidbodies.GetComponent(body->entity);
    if (rigidbody == nullptr) return ELISA_PHYSICS_INVALID_HANDLE;
    if (rigidbody->mass <= 0.0f || rigidbody->IsKinematic()) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    if (rigidbody->physicsobject != nullptr) return ELISA_PHYSICS_CONFIGURATION_LOCKED;

    const ElisaCoordinateProfile profile = elisa_coordinate_profile();
    const float inertia_scale = profile.metres_per_unit * profile.metres_per_unit;
    const XMFLOAT3 backend_moments(moment_x * inertia_scale,
        moment_y * inertia_scale, moment_z * inertia_scale);
    if (!probe::coordinates::finite(backend_moments)) return ELISA_PHYSICS_INVALID_ARGUMENT;
    const XMFLOAT4 backend_rotation = normalized_wicked_inertia_rotation(
        rotation_x, rotation_y, rotation_z, rotation_w);
    rigidbody->principal_inertia = backend_moments;
    rigidbody->principal_inertia_rotation = backend_rotation;
    rigidbody->has_principal_inertia_override = true;
    return ELISA_PHYSICS_OK;
}

extern "C" int32_t elisa_physics_v1_body_inertia(uint64_t world_generation,
    uint32_t slot, uint64_t body_generation, float* moment_x, float* moment_y,
    float* moment_z, float* rotation_x, float* rotation_y, float* rotation_z,
    float* rotation_w) {
    if (moment_x == nullptr || moment_y == nullptr || moment_z == nullptr ||
        rotation_x == nullptr || rotation_y == nullptr || rotation_z == nullptr ||
        rotation_w == nullptr) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    const int32_t status = require_world(world_generation);
    if (status != ELISA_PHYSICS_OK) return status;
    PhysicsService& state = physics_service();
    BodySlot* body = resolve_body(state, slot, body_generation);
    if (body == nullptr) return ELISA_PHYSICS_INVALID_HANDLE;
    wi::scene::RigidBodyPhysicsComponent* rigidbody =
        state.scene->rigidbodies.GetComponent(body->entity);
    if (rigidbody == nullptr) return ELISA_PHYSICS_INVALID_HANDLE;
    if (rigidbody->mass <= 0.0f || rigidbody->IsKinematic()) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    if (rigidbody->physicsobject == nullptr) return ELISA_PHYSICS_BODY_NOT_READY;

    const XMFLOAT3 backend_moments = wi::physics::GetPrincipalInertia(*rigidbody);
    const XMFLOAT4 wicked_rotation = wi::physics::GetPrincipalInertiaRotation(*rigidbody);
    const ElisaCoordinateProfile profile = elisa_coordinate_profile();
    const float inverse_scale = 1.0f / profile.metres_per_unit;
    const float inertia_scale = inverse_scale * inverse_scale;
    const XMFLOAT3 authored_moments(backend_moments.x * inertia_scale,
        backend_moments.y * inertia_scale, backend_moments.z * inertia_scale);
    if (!valid_principal_moments(authored_moments.x, authored_moments.y,
            authored_moments.z) || !valid_inertia_rotation(wicked_rotation.x,
            wicked_rotation.y, wicked_rotation.z, wicked_rotation.w)) {
        return ELISA_PHYSICS_BACKEND_FAILURE;
    }
    const XMFLOAT4 authored_rotation = normalized_wicked_inertia_rotation(
        wicked_rotation.x, wicked_rotation.y, wicked_rotation.z, wicked_rotation.w);
    *moment_x = authored_moments.x;
    *moment_y = authored_moments.y;
    *moment_z = authored_moments.z;
    *rotation_x = authored_rotation.x;
    *rotation_y = authored_rotation.y;
    *rotation_z = authored_rotation.z;
    *rotation_w = authored_rotation.w;
    return ELISA_PHYSICS_OK;
}
