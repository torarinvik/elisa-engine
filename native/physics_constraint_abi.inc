namespace {

constexpr float MAX_CONSTRAINT_LIMIT = 1.0e4f;
constexpr float MAX_CONSTRAINT_MOTOR_SPEED = 1.0e4f;
constexpr float MAX_CONSTRAINT_MOTOR_FORCE = 1.0e8f;
constexpr float MAX_CONSTRAINT_BREAK_DISTANCE = 1.0e4f;
constexpr float MIN_CONSTRAINT_ROTATION_LENGTH = 1.0e-6f;

bool valid_constraint_limit(float value) {
    return std::isfinite(value) && std::fabs(value) <= MAX_CONSTRAINT_LIMIT;
}

int32_t resolve_constraint_component(PhysicsService& state, uint32_t slot,
    uint64_t generation, ConstraintSlot*& record,
    wi::scene::PhysicsConstraintComponent*& component) {
    record = resolve_constraint(state, slot, generation);
    if (record == nullptr) return ELISA_PHYSICS_INVALID_HANDLE;
    component = state.scene->constraints.GetComponent(record->entity);
    return component == nullptr ? ELISA_PHYSICS_INVALID_HANDLE : ELISA_PHYSICS_OK;
}

bool convert_grab_ray(const ElisaCoordinateProfile& profile,
    float origin_x, float origin_y, float origin_z,
    float direction_x, float direction_y, float direction_z,
    wi::primitive::Ray& ray) {
    XMFLOAT3 origin{};
    XMFLOAT3 direction{};
    if (!elisa_physics_coordinates::to_wicked_position(&profile,
            XMFLOAT3(origin_x, origin_y, origin_z), origin) ||
        !elisa_physics_coordinates::to_wicked_direction(&profile,
            XMFLOAT3(direction_x, direction_y, direction_z), direction)) {
        return false;
    }
    const float length_squared = direction.x * direction.x +
        direction.y * direction.y + direction.z * direction.z;
    if (!std::isfinite(length_squared) || length_squared <= 1.0e-12f) return false;
    const float inverse_length = 1.0f / std::sqrt(length_squared);
    direction.x *= inverse_length;
    direction.y *= inverse_length;
    direction.z *= inverse_length;
    ray = wi::primitive::Ray(origin, direction);
    return true;
}

} // namespace

extern "C" int32_t elisa_physics_v1_create_constraint(uint64_t world_generation,
    uint32_t body_a_slot, uint64_t body_a_generation,
    uint32_t body_b_slot, uint64_t body_b_generation, int32_t kind,
    float anchor_x, float anchor_y, float anchor_z,
    float rotation_x, float rotation_y, float rotation_z, float rotation_w,
    float lower_limit, float upper_limit, float target_velocity,
    float max_motor_force, float break_distance, int32_t disable_collision,
    float cone_half_angle,
    float translation_min_x, float translation_min_y, float translation_min_z,
    float translation_max_x, float translation_max_y, float translation_max_z,
    float rotation_min_x, float rotation_min_y, float rotation_min_z,
    float rotation_max_x, float rotation_max_y, float rotation_max_z,
    float swing_normal_half_cone_angle, float swing_plane_half_cone_angle,
    float swing_min_twist_angle, float swing_max_twist_angle,
    uint32_t* slot, uint64_t* constraint_generation) {
    if (slot == nullptr || constraint_generation == nullptr ||
        kind < ELISA_PHYSICS_CONSTRAINT_FIXED || kind > ELISA_PHYSICS_CONSTRAINT_SWING_TWIST ||
        !std::isfinite(anchor_x) || !std::isfinite(anchor_y) || !std::isfinite(anchor_z) ||
        std::fabs(anchor_x) > MAX_POSITION || std::fabs(anchor_y) > MAX_POSITION ||
        std::fabs(anchor_z) > MAX_POSITION || !std::isfinite(rotation_x) ||
        !std::isfinite(rotation_y) || !std::isfinite(rotation_z) ||
        !std::isfinite(rotation_w) || !valid_constraint_limit(lower_limit) ||
        !valid_constraint_limit(upper_limit) || lower_limit > upper_limit ||
        !std::isfinite(target_velocity) ||
        std::fabs(target_velocity) > MAX_CONSTRAINT_MOTOR_SPEED ||
        !std::isfinite(max_motor_force) || max_motor_force < 0.0f ||
        max_motor_force > MAX_CONSTRAINT_MOTOR_FORCE || !std::isfinite(break_distance) ||
        break_distance < 0.0f || break_distance > MAX_CONSTRAINT_BREAK_DISTANCE ||
        !std::isfinite(cone_half_angle) || !valid_constraint_limit(translation_min_x) ||
        !valid_constraint_limit(translation_min_y) || !valid_constraint_limit(translation_min_z) ||
        !valid_constraint_limit(translation_max_x) || !valid_constraint_limit(translation_max_y) ||
        !valid_constraint_limit(translation_max_z) || !std::isfinite(rotation_min_x) ||
        !std::isfinite(rotation_min_y) || !std::isfinite(rotation_min_z) ||
        !std::isfinite(rotation_max_x) || !std::isfinite(rotation_max_y) ||
        !std::isfinite(rotation_max_z) || !std::isfinite(swing_normal_half_cone_angle) ||
        !std::isfinite(swing_plane_half_cone_angle) ||
        !std::isfinite(swing_min_twist_angle) || !std::isfinite(swing_max_twist_angle) ||
        (disable_collision != 0 && disable_collision != 1)) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    const double rotation_length = std::sqrt(double(rotation_x) * rotation_x +
        double(rotation_y) * rotation_y + double(rotation_z) * rotation_z +
        double(rotation_w) * rotation_w);
    if (!std::isfinite(rotation_length) || rotation_length <= MIN_CONSTRAINT_ROTATION_LENGTH) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    const bool has_motor = kind == ELISA_PHYSICS_CONSTRAINT_HINGE ||
        kind == ELISA_PHYSICS_CONSTRAINT_SLIDER;
    if ((!has_motor && (target_velocity != 0.0f || max_motor_force != 0.0f)) ||
        (target_velocity != 0.0f && max_motor_force <= 0.0f) ||
        ((kind == ELISA_PHYSICS_CONSTRAINT_FIXED || kind == ELISA_PHYSICS_CONSTRAINT_POINT) &&
            (lower_limit != 0.0f || upper_limit != 0.0f)) ||
        (kind != ELISA_PHYSICS_CONSTRAINT_SIX_DOF &&
            (translation_min_x != 0.0f || translation_min_y != 0.0f ||
             translation_min_z != 0.0f || translation_max_x != 0.0f ||
             translation_max_y != 0.0f || translation_max_z != 0.0f ||
             rotation_min_x != 0.0f || rotation_min_y != 0.0f ||
             rotation_min_z != 0.0f || rotation_max_x != 0.0f ||
             rotation_max_y != 0.0f || rotation_max_z != 0.0f)) ||
        (kind != ELISA_PHYSICS_CONSTRAINT_CONE && cone_half_angle != 0.0f) ||
        (kind != ELISA_PHYSICS_CONSTRAINT_DISTANCE &&
         kind != ELISA_PHYSICS_CONSTRAINT_HINGE && kind != ELISA_PHYSICS_CONSTRAINT_SLIDER &&
            (lower_limit != 0.0f || upper_limit != 0.0f)) ||
        (kind != ELISA_PHYSICS_CONSTRAINT_SWING_TWIST &&
            (swing_normal_half_cone_angle != 0.0f ||
             swing_plane_half_cone_angle != 0.0f || swing_min_twist_angle != 0.0f ||
             swing_max_twist_angle != 0.0f))) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    if (kind == ELISA_PHYSICS_CONSTRAINT_HINGE &&
        (lower_limit < -XM_PI || upper_limit > XM_PI)) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    if (kind == ELISA_PHYSICS_CONSTRAINT_CONE &&
        (cone_half_angle < 0.0f || cone_half_angle > XM_PI)) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    if (kind == ELISA_PHYSICS_CONSTRAINT_SIX_DOF &&
        (translation_min_x > translation_max_x || translation_min_y > translation_max_y ||
         translation_min_z > translation_max_z || rotation_min_x > rotation_max_x ||
         rotation_min_y > rotation_max_y || rotation_min_z > rotation_max_z ||
         rotation_min_x < -XM_PI || rotation_min_y < -XM_PI || rotation_min_z < -XM_PI ||
         rotation_max_x > XM_PI || rotation_max_y > XM_PI || rotation_max_z > XM_PI)) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    if (kind == ELISA_PHYSICS_CONSTRAINT_SWING_TWIST &&
        (swing_normal_half_cone_angle < 0.0f || swing_normal_half_cone_angle > XM_PI ||
         swing_plane_half_cone_angle < 0.0f || swing_plane_half_cone_angle > XM_PI ||
         swing_min_twist_angle < -XM_PI || swing_min_twist_angle > XM_PI ||
         swing_max_twist_angle < -XM_PI || swing_max_twist_angle > XM_PI ||
         swing_min_twist_angle > swing_max_twist_angle)) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }

    const int32_t world_status = require_world(world_generation);
    if (world_status != ELISA_PHYSICS_OK) return world_status;
    PhysicsService& state = physics_service();
    BodySlot* body_a = resolve_body(state, body_a_slot, body_a_generation);
    BodySlot* body_b = resolve_body(state, body_b_slot, body_b_generation);
    if (body_a == nullptr || body_b == nullptr) return ELISA_PHYSICS_INVALID_HANDLE;
    if (body_a_slot == body_b_slot && body_a_generation == body_b_generation) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    if (state.scene->rigidbodies.GetComponent(body_a->entity) == nullptr ||
        state.scene->rigidbodies.GetComponent(body_b->entity) == nullptr) {
        return ELISA_PHYSICS_INVALID_HANDLE;
    }

    uint32_t free_slot = MAX_CONSTRAINTS;
    for (uint32_t index = 0; index < MAX_CONSTRAINTS; ++index) {
        if (!state.constraints[index].live) { free_slot = index; break; }
    }
    if (free_slot == MAX_CONSTRAINTS) return ELISA_PHYSICS_CAPACITY;
    ConstraintSlot& record = state.constraints[free_slot];
    if (record.generation == UINT64_MAX) return ELISA_PHYSICS_CAPACITY;

    const ElisaCoordinateProfile profile = elisa_coordinate_profile();
    ElisaTransformPayload authored{{anchor_x, anchor_y, anchor_z},
        {rotation_x, rotation_y, rotation_z, rotation_w}, {1.0f, 1.0f, 1.0f}};
    const std::string name = "elisa_constraint_" + std::to_string(free_slot);
    wi::ecs::Entity entity = wi::ecs::INVALID_ENTITY;
    try {
        entity = state.scene->Entity_CreateTransform(name);
        wi::scene::TransformComponent* transform = state.scene->transforms.GetComponent(entity);
        if (transform == nullptr || !probe::submit_elisa_transform(&profile, &authored, transform)) {
            state.scene->Entity_Remove(entity);
            return ELISA_PHYSICS_INVALID_ARGUMENT;
        }
        wi::scene::PhysicsConstraintComponent& component = state.scene->constraints.Create(entity);
        component.bodyA = body_a->entity;
        component.bodyB = body_b->entity;
        component.SetDisableSelfCollision(disable_collision != 0);
        switch (kind) {
        case ELISA_PHYSICS_CONSTRAINT_FIXED:
            component.type = wi::scene::PhysicsConstraintComponent::Type::Fixed;
            break;
        case ELISA_PHYSICS_CONSTRAINT_POINT:
            component.type = wi::scene::PhysicsConstraintComponent::Type::Point;
            break;
        case ELISA_PHYSICS_CONSTRAINT_DISTANCE:
            component.type = wi::scene::PhysicsConstraintComponent::Type::Distance;
            if (!elisa_physics_coordinates::to_wicked_length(&profile, lower_limit,
                    component.distance_constraint.min_distance) ||
                !elisa_physics_coordinates::to_wicked_length(&profile, upper_limit,
                    component.distance_constraint.max_distance)) {
                state.scene->Entity_Remove(entity);
                return ELISA_PHYSICS_INVALID_ARGUMENT;
            }
            break;
        case ELISA_PHYSICS_CONSTRAINT_HINGE:
            component.type = wi::scene::PhysicsConstraintComponent::Type::Hinge;
            component.hinge_constraint.min_angle = lower_limit;
            component.hinge_constraint.max_angle = upper_limit;
            component.hinge_constraint.target_angular_velocity = target_velocity;
            component.hinge_constraint.max_torque = max_motor_force;
            break;
        case ELISA_PHYSICS_CONSTRAINT_SLIDER:
            component.type = wi::scene::PhysicsConstraintComponent::Type::Slider;
            component.slider_constraint.min_limit = lower_limit * profile.metres_per_unit;
            component.slider_constraint.max_limit = upper_limit * profile.metres_per_unit;
            component.slider_constraint.target_velocity = target_velocity * profile.metres_per_unit;
            component.slider_constraint.max_force = max_motor_force;
            if (!std::isfinite(component.slider_constraint.min_limit) ||
                !std::isfinite(component.slider_constraint.max_limit) ||
                !std::isfinite(component.slider_constraint.target_velocity)) {
                state.scene->Entity_Remove(entity);
                return ELISA_PHYSICS_INVALID_ARGUMENT;
            }
            break;
        case ELISA_PHYSICS_CONSTRAINT_CONE:
            component.type = wi::scene::PhysicsConstraintComponent::Type::Cone;
            component.cone_constraint.half_cone_angle = cone_half_angle;
            break;
        case ELISA_PHYSICS_CONSTRAINT_SIX_DOF:
            component.type = wi::scene::PhysicsConstraintComponent::Type::SixDOF;
            component.six_dof.minTranslationAxes = XMFLOAT3(
                translation_min_x * profile.metres_per_unit,
                translation_min_y * profile.metres_per_unit,
                translation_min_z * profile.metres_per_unit);
            component.six_dof.maxTranslationAxes = XMFLOAT3(
                translation_max_x * profile.metres_per_unit,
                translation_max_y * profile.metres_per_unit,
                translation_max_z * profile.metres_per_unit);
            component.six_dof.minRotationAxes = XMFLOAT3(
                rotation_min_x, rotation_min_y, rotation_min_z);
            component.six_dof.maxRotationAxes = XMFLOAT3(
                rotation_max_x, rotation_max_y, rotation_max_z);
            if (!std::isfinite(component.six_dof.minTranslationAxes.x) ||
                !std::isfinite(component.six_dof.minTranslationAxes.y) ||
                !std::isfinite(component.six_dof.minTranslationAxes.z) ||
                !std::isfinite(component.six_dof.maxTranslationAxes.x) ||
                !std::isfinite(component.six_dof.maxTranslationAxes.y) ||
                !std::isfinite(component.six_dof.maxTranslationAxes.z)) {
                state.scene->Entity_Remove(entity);
                return ELISA_PHYSICS_INVALID_ARGUMENT;
            }
            break;
        case ELISA_PHYSICS_CONSTRAINT_SWING_TWIST:
            component.type = wi::scene::PhysicsConstraintComponent::Type::SwingTwist;
            component.swing_twist.normal_half_cone_angle = swing_normal_half_cone_angle;
            component.swing_twist.plane_half_cone_angle = swing_plane_half_cone_angle;
            component.swing_twist.min_twist_angle = swing_min_twist_angle;
            component.swing_twist.max_twist_angle = swing_max_twist_angle;
            break;
        default:
            state.scene->Entity_Remove(entity);
            return ELISA_PHYSICS_INVALID_ARGUMENT;
        }
        component.break_distance = break_distance == 0.0f ? FLT_MAX
            : break_distance * profile.metres_per_unit;
        if (!std::isfinite(component.break_distance) && break_distance != 0.0f) {
            state.scene->Entity_Remove(entity);
            return ELISA_PHYSICS_INVALID_ARGUMENT;
        }
    } catch (...) {
        if (entity != wi::ecs::INVALID_ENTITY) state.scene->Entity_Remove(entity);
        return ELISA_PHYSICS_BACKEND_FAILURE;
    }

    ++record.generation;
    record.entity = entity;
    record.body_a_slot = body_a_slot;
    record.body_b_slot = body_b_slot;
    record.body_a_generation = body_a_generation;
    record.body_b_generation = body_b_generation;
    record.kind = kind;
    record.live = true;
    *slot = free_slot;
    *constraint_generation = record.generation;
    return ELISA_PHYSICS_OK;
}

extern "C" int32_t elisa_physics_v1_constraint_set_motor(uint64_t world_generation,
    uint32_t slot, uint64_t constraint_generation,
    float target_velocity, float max_motor_force) {
    if (!std::isfinite(target_velocity) ||
        std::fabs(target_velocity) > MAX_CONSTRAINT_MOTOR_SPEED ||
        !std::isfinite(max_motor_force) || max_motor_force < 0.0f ||
        max_motor_force > MAX_CONSTRAINT_MOTOR_FORCE ||
        (target_velocity != 0.0f && max_motor_force <= 0.0f)) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    const int32_t world_status = require_world(world_generation);
    if (world_status != ELISA_PHYSICS_OK) return world_status;
    PhysicsService& state = physics_service();
    ConstraintSlot* record = nullptr;
    wi::scene::PhysicsConstraintComponent* component = nullptr;
    const int32_t status = resolve_constraint_component(state, slot,
        constraint_generation, record, component);
    if (status != ELISA_PHYSICS_OK) return status;
    if (record->kind != ELISA_PHYSICS_CONSTRAINT_HINGE &&
        record->kind != ELISA_PHYSICS_CONSTRAINT_SLIDER) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    const ElisaCoordinateProfile profile = elisa_coordinate_profile();
    if (record->kind == ELISA_PHYSICS_CONSTRAINT_HINGE) {
        component->hinge_constraint.target_angular_velocity = target_velocity;
        component->hinge_constraint.max_torque = max_motor_force;
    } else {
        component->slider_constraint.target_velocity = target_velocity * profile.metres_per_unit;
        component->slider_constraint.max_force = max_motor_force;
        if (!std::isfinite(component->slider_constraint.target_velocity)) {
            return ELISA_PHYSICS_INVALID_ARGUMENT;
        }
    }
    component->SetRefreshParametersNeeded();
    return ELISA_PHYSICS_OK;
}

extern "C" int32_t elisa_physics_v1_constraint_is_broken(uint64_t world_generation,
    uint32_t slot, uint64_t constraint_generation, int32_t* broken) {
    if (broken == nullptr) return ELISA_PHYSICS_INVALID_ARGUMENT;
    const int32_t world_status = require_world(world_generation);
    if (world_status != ELISA_PHYSICS_OK) return world_status;
    PhysicsService& state = physics_service();
    ConstraintSlot* record = nullptr;
    wi::scene::PhysicsConstraintComponent* component = nullptr;
    const int32_t status = resolve_constraint_component(state, slot,
        constraint_generation, record, component);
    if (status != ELISA_PHYSICS_OK) return status;
    if (!wi::physics::IsConstraintReady(*component)) return ELISA_PHYSICS_BODY_NOT_READY;
    *broken = wi::physics::IsConstraintBroken(*component) ? 1 : 0;
    return ELISA_PHYSICS_OK;
}

extern "C" int32_t elisa_physics_v1_destroy_constraint(uint64_t world_generation,
    uint32_t slot, uint64_t constraint_generation) {
    const int32_t world_status = require_world(world_generation);
    if (world_status != ELISA_PHYSICS_OK) return world_status;
    PhysicsService& state = physics_service();
    ConstraintSlot* record = resolve_constraint(state, slot, constraint_generation);
    if (record == nullptr) return ELISA_PHYSICS_INVALID_HANDLE;
    destroy_constraint(state, *record);
    return ELISA_PHYSICS_OK;
}

extern "C" int32_t elisa_physics_v1_grab_begin(uint64_t world_generation,
    float origin_x, float origin_y, float origin_z,
    float direction_x, float direction_y, float direction_z,
    int32_t kind, float break_distance,
    uint32_t* slot, uint64_t* grab_generation) {
    if (slot == nullptr || grab_generation == nullptr ||
        (kind != ELISA_PHYSICS_GRAB_FIXED && kind != ELISA_PHYSICS_GRAB_POINT) ||
        !std::isfinite(break_distance) || break_distance < 0.0f ||
        break_distance > MAX_CONSTRAINT_BREAK_DISTANCE) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    const int32_t world_status = require_world(world_generation);
    if (world_status != ELISA_PHYSICS_OK) return world_status;
    PhysicsService& state = physics_service();
    if (state.scene->physics_scene == nullptr) return ELISA_PHYSICS_BODY_NOT_READY;
    uint32_t free_slot = MAX_GRABS;
    for (uint32_t index = 0; index < MAX_GRABS; ++index) {
        if (!state.grabs[index].live) { free_slot = index; break; }
    }
    if (free_slot == MAX_GRABS) return ELISA_PHYSICS_CAPACITY;
    GrabSlot& record = state.grabs[free_slot];
    if (record.generation == UINT64_MAX) return ELISA_PHYSICS_CAPACITY;
    wi::primitive::Ray ray;
    const ElisaCoordinateProfile profile = elisa_coordinate_profile();
    if (!convert_grab_ray(profile, origin_x, origin_y, origin_z,
            direction_x, direction_y, direction_z, ray)) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    float backend_break_distance = FLT_MAX;
    if (break_distance > 0.0f &&
        !elisa_physics_coordinates::to_wicked_length(&profile,
            break_distance, backend_break_distance)) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    try {
        const wi::physics::ConstraintType constraint_type = kind == ELISA_PHYSICS_GRAB_FIXED
            ? wi::physics::ConstraintType::Fixed : wi::physics::ConstraintType::Point;
        wi::physics::PickDrag(*state.scene, ray, record.operation,
            constraint_type, backend_break_distance);
    } catch (...) {
        destroy_grab(record);
        return ELISA_PHYSICS_BACKEND_FAILURE;
    }
    if (!record.operation.IsValid()) return ELISA_PHYSICS_NO_HIT;
    ++record.generation;
    record.kind = kind;
    record.break_distance = backend_break_distance;
    record.live = true;
    *slot = free_slot;
    *grab_generation = record.generation;
    return ELISA_PHYSICS_OK;
}

extern "C" int32_t elisa_physics_v1_grab_update(uint64_t world_generation,
    uint32_t slot, uint64_t grab_generation,
    float origin_x, float origin_y, float origin_z,
    float direction_x, float direction_y, float direction_z) {
    const int32_t world_status = require_world(world_generation);
    if (world_status != ELISA_PHYSICS_OK) return world_status;
    PhysicsService& state = physics_service();
    if (slot >= MAX_GRABS || !state.grabs[slot].live ||
        state.grabs[slot].generation != grab_generation) {
        return ELISA_PHYSICS_INVALID_HANDLE;
    }
    GrabSlot& record = state.grabs[slot];
    wi::primitive::Ray ray;
    const ElisaCoordinateProfile profile = elisa_coordinate_profile();
    if (!convert_grab_ray(profile, origin_x, origin_y, origin_z,
            direction_x, direction_y, direction_z, ray)) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    try {
        const wi::physics::ConstraintType constraint_type = record.kind == ELISA_PHYSICS_GRAB_FIXED
            ? wi::physics::ConstraintType::Fixed : wi::physics::ConstraintType::Point;
        wi::physics::PickDrag(*state.scene, ray, record.operation,
            constraint_type, record.break_distance);
    } catch (...) {
        destroy_grab(record);
        return ELISA_PHYSICS_BACKEND_FAILURE;
    }
    return ELISA_PHYSICS_OK;
}

extern "C" int32_t elisa_physics_v1_grab_end(uint64_t world_generation,
    uint32_t slot, uint64_t grab_generation) {
    const int32_t world_status = require_world(world_generation);
    if (world_status != ELISA_PHYSICS_OK) return world_status;
    PhysicsService& state = physics_service();
    if (slot >= MAX_GRABS || !state.grabs[slot].live ||
        state.grabs[slot].generation != grab_generation) {
        return ELISA_PHYSICS_INVALID_HANDLE;
    }
    destroy_grab(state.grabs[slot]);
    return ELISA_PHYSICS_OK;
}
