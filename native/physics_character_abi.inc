namespace {

constexpr float CHARACTER_MAX_RADIUS = 100.0f;
constexpr float CHARACTER_MAX_HALF_HEIGHT = 100.0f;
constexpr float CHARACTER_MAX_SPEED = 1000.0f;
constexpr float CHARACTER_MAX_GRAVITY_FACTOR = 8.0f;
constexpr float CHARACTER_MAX_SLOPE_ANGLE = 1.57079632679f;
constexpr float CHARACTER_HORIZONTAL_EPSILON = 1.0e-5f;

int32_t resolve_character(PhysicsService& state, uint32_t slot,
    uint64_t generation, wi::scene::RigidBodyPhysicsComponent*& result) {
    BodySlot* body = resolve_body(state, slot, generation);
    if (body == nullptr) return ELISA_PHYSICS_INVALID_HANDLE;
    if (!body->character) return ELISA_PHYSICS_INVALID_ARGUMENT;
    result = state.scene->rigidbodies.GetComponent(body->entity);
    if (result == nullptr || !result->IsCharacterPhysics()) return ELISA_PHYSICS_INVALID_HANDLE;
    if (result->physicsobject == nullptr) return ELISA_PHYSICS_BODY_NOT_READY;
    return ELISA_PHYSICS_OK;
}

bool valid_character_scalar(float value, float maximum) {
    return std::isfinite(value) && value >= 0.0f && value <= maximum;
}

} // namespace

extern "C" int32_t elisa_physics_v1_create_character(uint64_t world_generation,
    float position_x, float position_y, float position_z,
    float rotation_x, float rotation_y, float rotation_z, float rotation_w,
    float radius, float cylinder_half_height, float mass,
    float max_slope_angle, float gravity_factor, uint32_t collision_layer,
    uint32_t* slot, uint64_t* body_generation) {
    if (slot == nullptr || body_generation == nullptr ||
        !std::isfinite(radius) || radius <= 0.0f || radius > CHARACTER_MAX_RADIUS ||
        !valid_character_scalar(cylinder_half_height, CHARACTER_MAX_HALF_HEIGHT) ||
        !std::isfinite(mass) || mass <= 0.0f || mass > MAX_MASS ||
        !std::isfinite(max_slope_angle) || max_slope_angle < 0.0f ||
        max_slope_angle >= CHARACTER_MAX_SLOPE_ANGLE ||
        !valid_character_scalar(gravity_factor, CHARACTER_MAX_GRAVITY_FACTOR)) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    uint32_t body_slot = 0;
    uint64_t generation = 0;
    const int32_t create_status = elisa_physics_v1_create_body(world_generation,
        ELISA_PHYSICS_BODY_DYNAMIC, ELISA_PHYSICS_SHAPE_CAPSULE,
        position_x, position_y, position_z,
        rotation_x, rotation_y, rotation_z, rotation_w,
        0.0f, 0.0f, 0.0f, radius, cylinder_half_height, 0.0f, mass,
        0, collision_layer, &body_slot, &generation);
    if (create_status != ELISA_PHYSICS_OK) return create_status;

    PhysicsService& state = physics_service();
    BodySlot* body = resolve_body(state, body_slot, generation);
    wi::scene::RigidBodyPhysicsComponent* rigidbody = body == nullptr ? nullptr
        : state.scene->rigidbodies.GetComponent(body->entity);
    if (rigidbody == nullptr || rigidbody->physicsobject != nullptr) {
        (void)elisa_physics_v1_destroy_body(world_generation, body_slot, generation);
        return rigidbody == nullptr ? ELISA_PHYSICS_BACKEND_FAILURE
            : ELISA_PHYSICS_CONFIGURATION_LOCKED;
    }
    rigidbody->SetCharacterPhysics();
    rigidbody->character.maxSlopeAngle = max_slope_angle;
    rigidbody->character.gravityFactor = gravity_factor;
    body->character = true;
    *slot = body_slot;
    *body_generation = generation;
    return ELISA_PHYSICS_OK;
}

extern "C" int32_t elisa_physics_v1_character_move(uint64_t world_generation,
    uint32_t slot, uint64_t body_generation,
    float direction_x, float direction_y, float direction_z,
    float movement_speed, float jump_speed, int32_t control_in_air) {
    if (!std::isfinite(direction_x) || !std::isfinite(direction_y) ||
        !std::isfinite(direction_z) || std::fabs(direction_y) > CHARACTER_HORIZONTAL_EPSILON ||
        !valid_character_scalar(movement_speed, CHARACTER_MAX_SPEED) ||
        !valid_character_scalar(jump_speed, CHARACTER_MAX_SPEED) ||
        (control_in_air != 0 && control_in_air != 1)) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    const int32_t status = require_world(world_generation);
    if (status != ELISA_PHYSICS_OK) return status;
    PhysicsService& state = physics_service();
    wi::scene::RigidBodyPhysicsComponent* rigidbody = nullptr;
    const int32_t character_status = resolve_character(state, slot,
        body_generation, rigidbody);
    if (character_status != ELISA_PHYSICS_OK) return character_status;
    const ElisaCoordinateProfile profile = elisa_coordinate_profile();
    XMFLOAT3 backend_direction{};
    float backend_speed = 0.0f;
    float backend_jump_speed = 0.0f;
    if (!elisa_physics_coordinates::to_wicked_direction(&profile,
            XMFLOAT3(direction_x, direction_y, direction_z), backend_direction) ||
        !elisa_physics_coordinates::to_wicked_length(&profile,
            movement_speed, backend_speed) ||
        !elisa_physics_coordinates::to_wicked_length(&profile,
            jump_speed, backend_jump_speed)) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    const float length_squared = backend_direction.x * backend_direction.x +
        backend_direction.z * backend_direction.z;
    if (length_squared > 1.0f) {
        const float inverse_length = 1.0f / std::sqrt(length_squared);
        backend_direction.x *= inverse_length;
        backend_direction.z *= inverse_length;
    }
    try {
        wi::physics::MoveCharacter(*rigidbody, backend_direction, backend_speed,
            backend_jump_speed, control_in_air != 0);
    } catch (...) {
        return ELISA_PHYSICS_BACKEND_FAILURE;
    }
    return ELISA_PHYSICS_OK;
}

extern "C" int32_t elisa_physics_v1_character_info(uint64_t world_generation,
    uint32_t slot, uint64_t body_generation, int32_t* ground_state,
    float* ground_x, float* ground_y, float* ground_z,
    float* normal_x, float* normal_y, float* normal_z,
    float* velocity_x, float* velocity_y, float* velocity_z) {
    if (ground_state == nullptr || ground_x == nullptr || ground_y == nullptr ||
        ground_z == nullptr || normal_x == nullptr || normal_y == nullptr ||
        normal_z == nullptr || velocity_x == nullptr || velocity_y == nullptr ||
        velocity_z == nullptr) return ELISA_PHYSICS_INVALID_ARGUMENT;
    const int32_t status = require_world(world_generation);
    if (status != ELISA_PHYSICS_OK) return status;
    PhysicsService& state = physics_service();
    wi::scene::RigidBodyPhysicsComponent* rigidbody = nullptr;
    const int32_t character_status = resolve_character(state, slot,
        body_generation, rigidbody);
    if (character_status != ELISA_PHYSICS_OK) return character_status;
    try {
        const ElisaCoordinateProfile profile = elisa_coordinate_profile();
        const XMFLOAT3 backend_ground = wi::physics::GetCharacterGroundPosition(*rigidbody);
        const XMFLOAT3 backend_normal = wi::physics::GetCharacterGroundNormal(*rigidbody);
        const XMFLOAT3 backend_velocity = wi::physics::GetCharacterGroundVelocity(*rigidbody);
        XMFLOAT3 ground{};
        XMFLOAT3 normal{};
        XMFLOAT3 velocity{};
        if (!elisa_physics_coordinates::from_wicked_position(&profile,
                backend_ground, ground) ||
            !elisa_physics_coordinates::from_wicked_direction(&profile,
                backend_normal, normal) ||
            !elisa_physics_coordinates::from_wicked_physical_vector(&profile,
                backend_velocity, velocity)) return ELISA_PHYSICS_BACKEND_FAILURE;
        const wi::physics::CharacterGroundStates backend_state =
            wi::physics::GetCharacterGroundState(*rigidbody);
        switch (backend_state) {
        case wi::physics::CharacterGroundStates::OnGround: *ground_state = 0; break;
        case wi::physics::CharacterGroundStates::OnSteepGround: *ground_state = 1; break;
        case wi::physics::CharacterGroundStates::NotSupported: *ground_state = 2; break;
        case wi::physics::CharacterGroundStates::InAir: *ground_state = 3; break;
        default: return ELISA_PHYSICS_BACKEND_FAILURE;
        }
        *ground_x = ground.x;
        *ground_y = ground.y;
        *ground_z = ground.z;
        *normal_x = normal.x;
        *normal_y = normal.y;
        *normal_z = normal.z;
        *velocity_x = velocity.x;
        *velocity_y = velocity.y;
        *velocity_z = velocity.z;
    } catch (...) {
        return ELISA_PHYSICS_BACKEND_FAILURE;
    }
    return ELISA_PHYSICS_OK;
}

extern "C" int32_t elisa_physics_v1_character_set_capsule(uint64_t world_generation,
    uint32_t slot, uint64_t body_generation, float radius,
    float cylinder_half_height, int32_t* changed) {
    if (changed == nullptr || !std::isfinite(radius) || radius <= 0.0f ||
        radius > CHARACTER_MAX_RADIUS ||
        !valid_character_scalar(cylinder_half_height, CHARACTER_MAX_HALF_HEIGHT)) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    const int32_t status = require_world(world_generation);
    if (status != ELISA_PHYSICS_OK) return status;
    PhysicsService& state = physics_service();
    wi::scene::RigidBodyPhysicsComponent* rigidbody = nullptr;
    const int32_t character_status = resolve_character(state, slot,
        body_generation, rigidbody);
    if (character_status != ELISA_PHYSICS_OK) return character_status;
    const ElisaCoordinateProfile profile = elisa_coordinate_profile();
    float backend_radius = 0.0f;
    float backend_half_height = 0.0f;
    if (!elisa_physics_coordinates::to_wicked_length(&profile,
            radius, backend_radius) ||
        !elisa_physics_coordinates::to_wicked_length(&profile,
            cylinder_half_height, backend_half_height)) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    try {
        const wi::scene::RigidBodyPhysicsComponent::CapsuleParams capsule{
            backend_radius, backend_half_height};
        *changed = wi::physics::ChangeCharacterShape(*rigidbody, capsule) ? 1 : 0;
    } catch (...) {
        return ELISA_PHYSICS_BACKEND_FAILURE;
    }
    return ELISA_PHYSICS_OK;
}
