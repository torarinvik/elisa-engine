namespace {

constexpr uint32_t CAPSULE_HEMISPHERE_STEPS = 8;
constexpr uint32_t CAPSULE_RADIAL_SEGMENTS = 16;
constexpr float ZERO_DIMENSION = 0.0f;
constexpr float ZERO_MASS = 0.0f;
constexpr float UNIT_SHAPE_RADIUS = 1.0f;
constexpr float UNIT_BOX_HALF_EXTENT = 1.0f;
constexpr double MIN_BODY_CREATE_ROTATION_LENGTH = 1.0e-6;
constexpr int32_t SENSOR_DISABLED = 0;
constexpr int32_t SENSOR_ENABLED = 1;
constexpr uint32_t CAPSULE_RING_COUNT = CAPSULE_HEMISPHERE_STEPS * 2 + 2;
constexpr size_t CAPSULE_VERTEX_COUNT =
    size_t(CAPSULE_RING_COUNT) * CAPSULE_RADIAL_SEGMENTS;
constexpr size_t CAPSULE_INDEX_COUNT =
    size_t(CAPSULE_RING_COUNT - 1) * CAPSULE_RADIAL_SEGMENTS * 6;

wi::ecs::Entity create_capsule_proxy(wi::scene::Scene& scene, const std::string& name,
    float half_cylinder_height) {
    std::array<XMFLOAT3, CAPSULE_VERTEX_COUNT> positions{};
    std::array<uint32_t, CAPSULE_INDEX_COUNT> indices{};
    uint32_t ring = 0;
    for (uint32_t step = 0; step <= CAPSULE_HEMISPHERE_STEPS; ++step) {
        const float angle = XM_PIDIV2 * float(step) / float(CAPSULE_HEMISPHERE_STEPS);
        const float radial = std::sin(angle);
        const float y = half_cylinder_height + std::cos(angle);
        for (uint32_t segment = 0; segment < CAPSULE_RADIAL_SEGMENTS; ++segment) {
            const float around = XM_2PI * float(segment) / float(CAPSULE_RADIAL_SEGMENTS);
            positions[size_t(ring) * CAPSULE_RADIAL_SEGMENTS + segment] =
                XMFLOAT3(radial * std::cos(around), y, radial * std::sin(around));
        }
        ++ring;
    }
    for (uint32_t step = 0; step <= CAPSULE_HEMISPHERE_STEPS; ++step) {
        const float angle = XM_PIDIV2 *
            (1.0f - float(step) / float(CAPSULE_HEMISPHERE_STEPS));
        const float radial = std::sin(angle);
        const float y = -half_cylinder_height - std::cos(angle);
        for (uint32_t segment = 0; segment < CAPSULE_RADIAL_SEGMENTS; ++segment) {
            const float around = XM_2PI * float(segment) / float(CAPSULE_RADIAL_SEGMENTS);
            positions[size_t(ring) * CAPSULE_RADIAL_SEGMENTS + segment] =
                XMFLOAT3(radial * std::cos(around), y, radial * std::sin(around));
        }
        ++ring;
    }

    size_t cursor = 0;
    for (uint32_t row = 0; row + 1 < CAPSULE_RING_COUNT; ++row) {
        for (uint32_t segment = 0; segment < CAPSULE_RADIAL_SEGMENTS; ++segment) {
            const uint32_t next = (segment + 1) % CAPSULE_RADIAL_SEGMENTS;
            const uint32_t upper = row * CAPSULE_RADIAL_SEGMENTS + segment;
            const uint32_t upper_next = row * CAPSULE_RADIAL_SEGMENTS + next;
            const uint32_t lower = (row + 1) * CAPSULE_RADIAL_SEGMENTS + segment;
            const uint32_t lower_next = (row + 1) * CAPSULE_RADIAL_SEGMENTS + next;
            indices[cursor++] = upper;
            indices[cursor++] = upper_next;
            indices[cursor++] = lower;
            indices[cursor++] = upper_next;
            indices[cursor++] = lower_next;
            indices[cursor++] = lower;
        }
    }
    return scene.Entity_CreateMeshFromData(name, indices.size(), indices.data(),
        positions.size(), positions.data());
}

} // namespace

extern "C" int32_t elisa_physics_v1_create_body(uint64_t world_generation, int32_t kind,
    int32_t shape,
    float position_x, float position_y, float position_z,
    float rotation_x, float rotation_y, float rotation_z, float rotation_w,
    float center_of_mass_x, float center_of_mass_y, float center_of_mass_z,
    float dimension_x, float dimension_y, float dimension_z, float mass,
    int32_t sensor, uint32_t collision_layer,
    uint32_t* slot, uint64_t* body_generation) {
    if (slot == nullptr || body_generation == nullptr ||
        kind < ELISA_PHYSICS_BODY_STATIC || kind > ELISA_PHYSICS_BODY_DYNAMIC ||
        shape < ELISA_PHYSICS_SHAPE_BOX || shape > ELISA_PHYSICS_SHAPE_CAPSULE ||
        collision_layer >= wi::physics::COLLISION_LAYER_COUNT ||
        (sensor != SENSOR_DISABLED && sensor != SENSOR_ENABLED) ||
        !std::isfinite(position_x) || !std::isfinite(position_y) || !std::isfinite(position_z) ||
        !std::isfinite(rotation_x) || !std::isfinite(rotation_y) ||
        !std::isfinite(rotation_z) || !std::isfinite(rotation_w) ||
        !std::isfinite(center_of_mass_x) || !std::isfinite(center_of_mass_y) ||
        !std::isfinite(center_of_mass_z) ||
        std::fabs(center_of_mass_x) > MAX_CENTER_OF_MASS_OFFSET ||
        std::fabs(center_of_mass_y) > MAX_CENTER_OF_MASS_OFFSET ||
        std::fabs(center_of_mass_z) > MAX_CENTER_OF_MASS_OFFSET ||
        std::fabs(position_x) > MAX_POSITION || std::fabs(position_y) > MAX_POSITION ||
        std::fabs(position_z) > MAX_POSITION || !std::isfinite(dimension_x) ||
        !std::isfinite(dimension_y) || !std::isfinite(dimension_z) ||
        dimension_x <= ZERO_DIMENSION || dimension_x > MAX_HALF_EXTENT ||
        dimension_y < ZERO_DIMENSION || dimension_y > MAX_HALF_EXTENT ||
        dimension_z < ZERO_DIMENSION || dimension_z > MAX_HALF_EXTENT || !std::isfinite(mass) ||
        mass < ZERO_MASS || mass > MAX_MASS ||
        (kind == ELISA_PHYSICS_BODY_DYNAMIC && mass <= ZERO_MASS)) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    const double rotation_length = std::sqrt(double(rotation_x) * rotation_x +
        double(rotation_y) * rotation_y + double(rotation_z) * rotation_z +
        double(rotation_w) * rotation_w);
    if (!std::isfinite(rotation_length) ||
        rotation_length <= MIN_BODY_CREATE_ROTATION_LENGTH) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    if ((shape == ELISA_PHYSICS_SHAPE_BOX &&
            (dimension_y <= ZERO_DIMENSION || dimension_z <= ZERO_DIMENSION)) ||
        (shape == ELISA_PHYSICS_SHAPE_SPHERE &&
            (dimension_y != ZERO_DIMENSION || dimension_z != ZERO_DIMENSION)) ||
        (shape == ELISA_PHYSICS_SHAPE_CAPSULE &&
            (dimension_z != ZERO_DIMENSION || dimension_x + dimension_y > MAX_HALF_EXTENT))) {
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
    const ElisaCoordinateProfile profile = elisa_coordinate_profile();
    XMFLOAT3 backend_center_of_mass_offset;
    if (!elisa_physics_coordinates::to_wicked_physical_vector(&profile,
            XMFLOAT3(center_of_mass_x, center_of_mass_y, center_of_mass_z),
            backend_center_of_mass_offset) ||
        std::fabs(backend_center_of_mass_offset.x) > MAX_CENTER_OF_MASS_OFFSET ||
        std::fabs(backend_center_of_mass_offset.y) > MAX_CENTER_OF_MASS_OFFSET ||
        std::fabs(backend_center_of_mass_offset.z) > MAX_CENTER_OF_MASS_OFFSET) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }

    wi::ecs::Entity entity = wi::ecs::INVALID_ENTITY;
    try {
        const std::string body_name = "elisa_physics_body_" + std::to_string(free_slot);
        float scale_x = dimension_x;
        float scale_y = dimension_x;
        float scale_z = dimension_x;
        if (shape == ELISA_PHYSICS_SHAPE_BOX) {
            entity = state.scene->Entity_CreateCube(body_name);
            scale_y = dimension_y;
            scale_z = dimension_z;
        } else if (shape == ELISA_PHYSICS_SHAPE_SPHERE) {
            entity = state.scene->Entity_CreateSphere(body_name, UNIT_SHAPE_RADIUS,
                CAPSULE_RADIAL_SEGMENTS, CAPSULE_RADIAL_SEGMENTS);
        } else if (dimension_y == ZERO_DIMENSION) {
            entity = state.scene->Entity_CreateSphere(body_name, UNIT_SHAPE_RADIUS,
                CAPSULE_RADIAL_SEGMENTS, CAPSULE_RADIAL_SEGMENTS);
        } else {
            entity = create_capsule_proxy(*state.scene, body_name, dimension_y / dimension_x);
        }
        if (entity == wi::ecs::INVALID_ENTITY) return ELISA_PHYSICS_BACKEND_FAILURE;
        wi::scene::TransformComponent* transform = state.scene->transforms.GetComponent(entity);
        if (transform == nullptr) {
            state.scene->Entity_Remove(entity);
            return ELISA_PHYSICS_BACKEND_FAILURE;
        }
        const ElisaTransformPayload authored{{position_x, position_y, position_z},
            {rotation_x, rotation_y, rotation_z, rotation_w},
            {scale_x, scale_y, scale_z}};
        if (!probe::submit_elisa_transform(&profile, &authored, transform)) {
            state.scene->Entity_Remove(entity);
            return ELISA_PHYSICS_INVALID_ARGUMENT;
        }
        // Prime Wicked's world matrix before its physics pass reads entity scale.
        transform->UpdateTransform();
        set_body_collision_layer(*state.scene, entity, collision_layer);

        wi::scene::RigidBodyPhysicsComponent& rigidbody = state.scene->rigidbodies.Create(entity);
        rigidbody.center_of_mass_offset = backend_center_of_mass_offset;
        rigidbody.mass = kind == ELISA_PHYSICS_BODY_STATIC ? ZERO_MASS : mass;
        if (shape == ELISA_PHYSICS_SHAPE_BOX) {
            rigidbody.shape = wi::scene::RigidBodyPhysicsComponent::BOX;
            rigidbody.box.halfextents = XMFLOAT3(UNIT_BOX_HALF_EXTENT,
                UNIT_BOX_HALF_EXTENT, UNIT_BOX_HALF_EXTENT);
        } else if (shape == ELISA_PHYSICS_SHAPE_SPHERE) {
            rigidbody.shape = wi::scene::RigidBodyPhysicsComponent::SPHERE;
            rigidbody.sphere.radius = UNIT_SHAPE_RADIUS;
        } else if (dimension_y == ZERO_DIMENSION) {
            // A zero-length capsule is a sphere in Jolt; match its query proxy.
            rigidbody.shape = wi::scene::RigidBodyPhysicsComponent::SPHERE;
            rigidbody.sphere.radius = UNIT_SHAPE_RADIUS;
        } else {
            rigidbody.shape = wi::scene::RigidBodyPhysicsComponent::CAPSULE;
            rigidbody.capsule.radius = UNIT_SHAPE_RADIUS;
            // Jolt's capsule setting takes the cylindrical half-height.
            // Entity scale supplies radius, so store the authored ratio here.
            rigidbody.capsule.height = dimension_y / dimension_x;
        }
        rigidbody.SetKinematic(kind == ELISA_PHYSICS_BODY_KINEMATIC);
        rigidbody.SetSensor(sensor == SENSOR_ENABLED);
        rigidbody.collision_layer = collision_layer;
        rigidbody.collision_filter_subgroup = free_slot;
        if (!wi::physics::RegisterCollisionLayerBody(*state.scene, free_slot, collision_layer)) {
            state.scene->Entity_Remove(entity);
            return ELISA_PHYSICS_BACKEND_FAILURE;
        }
        body_slot.entity = entity;
        ++body_slot.generation;
        body_slot.live = true;
        body_slot.character = false;
    } catch (...) {
        if (entity != wi::ecs::INVALID_ENTITY) state.scene->Entity_Remove(entity);
        return ELISA_PHYSICS_BACKEND_FAILURE;
    }
    *slot = free_slot;
    *body_generation = body_slot.generation;
    return ELISA_PHYSICS_OK;
}

extern "C" int32_t elisa_physics_v1_create_box(uint64_t world_generation, int32_t kind,
    float position_x, float position_y, float position_z,
    float half_x, float half_y, float half_z, float mass,
    int32_t sensor,
    uint32_t* slot, uint64_t* body_generation) {
    return elisa_physics_v1_create_body(world_generation, kind, ELISA_PHYSICS_SHAPE_BOX,
        position_x, position_y, position_z, 0.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 0.0f,
        half_x, half_y, half_z, mass, sensor,
        0,
        slot, body_generation);
}
