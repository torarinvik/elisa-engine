namespace {

constexpr float MAX_BODY_LINEAR_SPEED = 10000.0f;
constexpr float MAX_BODY_IMPULSE_COMPONENT = 10000000.0f;

bool finite_bounded_vector(float x, float y, float z, float maximum) {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) &&
        std::fabs(x) <= maximum && std::fabs(y) <= maximum && std::fabs(z) <= maximum;
}

int32_t resolve_dynamic_rigidbody(PhysicsService& state, uint32_t slot,
    uint64_t generation, wi::scene::RigidBodyPhysicsComponent*& result) {
    BodySlot* body = resolve_body(state, slot, generation);
    if (body == nullptr) return ELISA_PHYSICS_INVALID_HANDLE;
    result = state.scene->rigidbodies.GetComponent(body->entity);
    if (result == nullptr) return ELISA_PHYSICS_INVALID_HANDLE;
    if (result->mass <= 0.0f || result->IsKinematic()) return ELISA_PHYSICS_INVALID_ARGUMENT;
    if (result->physicsobject == nullptr) return ELISA_PHYSICS_INVALID_STATE;
    return ELISA_PHYSICS_OK;
}

} // namespace

extern "C" int32_t elisa_physics_v1_body_linear_velocity(uint64_t world_generation,
    uint32_t slot, uint64_t body_generation,
    float* velocity_x, float* velocity_y, float* velocity_z) {
    if (velocity_x == nullptr || velocity_y == nullptr || velocity_z == nullptr) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    const int32_t status = require_world(world_generation);
    if (status != ELISA_PHYSICS_OK) return status;
    PhysicsService& state = physics_service();
    wi::scene::RigidBodyPhysicsComponent* rigidbody = nullptr;
    const int32_t body_status = resolve_dynamic_rigidbody(state, slot,
        body_generation, rigidbody);
    if (body_status != ELISA_PHYSICS_OK) return body_status;
    try {
        const XMFLOAT3 velocity = wi::physics::GetVelocity(*rigidbody);
        if (!finite_bounded_vector(velocity.x, velocity.y, velocity.z,
                MAX_BODY_LINEAR_SPEED)) {
            return ELISA_PHYSICS_BACKEND_FAILURE;
        }
        *velocity_x = velocity.x;
        *velocity_y = velocity.y;
        *velocity_z = velocity.z;
    } catch (...) {
        return ELISA_PHYSICS_BACKEND_FAILURE;
    }
    return ELISA_PHYSICS_OK;
}

extern "C" int32_t elisa_physics_v1_body_set_linear_velocity(uint64_t world_generation,
    uint32_t slot, uint64_t body_generation,
    float velocity_x, float velocity_y, float velocity_z) {
    if (!finite_bounded_vector(velocity_x, velocity_y, velocity_z,
            MAX_BODY_LINEAR_SPEED)) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    const int32_t status = require_world(world_generation);
    if (status != ELISA_PHYSICS_OK) return status;
    PhysicsService& state = physics_service();
    wi::scene::RigidBodyPhysicsComponent* rigidbody = nullptr;
    const int32_t body_status = resolve_dynamic_rigidbody(state, slot,
        body_generation, rigidbody);
    if (body_status != ELISA_PHYSICS_OK) return body_status;
    try {
        wi::physics::SetLinearVelocity(*rigidbody,
            XMFLOAT3(velocity_x, velocity_y, velocity_z));
    } catch (...) {
        return ELISA_PHYSICS_BACKEND_FAILURE;
    }
    return ELISA_PHYSICS_OK;
}

extern "C" int32_t elisa_physics_v1_body_apply_impulse(uint64_t world_generation,
    uint32_t slot, uint64_t body_generation,
    float impulse_x, float impulse_y, float impulse_z) {
    if (!finite_bounded_vector(impulse_x, impulse_y, impulse_z,
            MAX_BODY_IMPULSE_COMPONENT)) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    const int32_t status = require_world(world_generation);
    if (status != ELISA_PHYSICS_OK) return status;
    PhysicsService& state = physics_service();
    wi::scene::RigidBodyPhysicsComponent* rigidbody = nullptr;
    const int32_t body_status = resolve_dynamic_rigidbody(state, slot,
        body_generation, rigidbody);
    if (body_status != ELISA_PHYSICS_OK) return body_status;
    try {
        wi::physics::ApplyImpulse(*rigidbody, XMFLOAT3(impulse_x, impulse_y, impulse_z));
    } catch (...) {
        return ELISA_PHYSICS_BACKEND_FAILURE;
    }
    return ELISA_PHYSICS_OK;
}
