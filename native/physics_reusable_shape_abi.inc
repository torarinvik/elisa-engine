namespace {

using namespace elisa_physics_internal;

ShapeSlot* resolve_shape(PhysicsService& state, uint32_t slot, uint64_t generation) {
    if (slot >= MAX_SHAPES) return nullptr;
    ShapeSlot& shape = state.shapes[slot];
    return shape.live && shape.generation == generation ? &shape : nullptr;
}

bool valid_shape_dimensions(int32_t kind, float x, float y, float z) {
    if (kind < ELISA_PHYSICS_SHAPE_BOX || kind > ELISA_PHYSICS_SHAPE_CAPSULE ||
        !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        x <= ZERO_DIMENSION || x > MAX_HALF_EXTENT || y < ZERO_DIMENSION ||
        y > MAX_HALF_EXTENT || z < ZERO_DIMENSION || z > MAX_HALF_EXTENT) {
        return false;
    }
    if (kind == ELISA_PHYSICS_SHAPE_BOX) return y > ZERO_DIMENSION && z > ZERO_DIMENSION;
    if (kind == ELISA_PHYSICS_SHAPE_SPHERE) return y == ZERO_DIMENSION && z == ZERO_DIMENSION;
    return z == ZERO_DIMENSION && x + y <= MAX_HALF_EXTENT;
}

void configure_precomputed_shape(ShapeSlot& shape) {
    wi::scene::RigidBodyPhysicsComponent& component = shape.backend_shape;
    component.mass = 1.0f;
    component.box.halfextents = XMFLOAT3(shape.dimension_x, shape.dimension_y,
        shape.dimension_z);
    if (shape.kind == ELISA_PHYSICS_SHAPE_BOX) {
        component.shape = wi::scene::RigidBodyPhysicsComponent::BOX;
    } else if (shape.kind == ELISA_PHYSICS_SHAPE_SPHERE || shape.dimension_y == ZERO_DIMENSION) {
        component.shape = wi::scene::RigidBodyPhysicsComponent::SPHERE;
        component.sphere.radius = shape.dimension_x;
    } else {
        component.shape = wi::scene::RigidBodyPhysicsComponent::CAPSULE;
        component.capsule.radius = shape.dimension_x;
        component.capsule.height = 2.0f * shape.dimension_y;
    }
    wi::physics::CreateRigidBodyShape(component, XMFLOAT3(1.0f, 1.0f, 1.0f), nullptr);
}

} // namespace

extern "C" int32_t elisa_physics_v1_create_shape(uint64_t world_generation, int32_t kind,
    float dimension_x, float dimension_y, float dimension_z,
    uint32_t* slot, uint64_t* shape_generation) {
    if (slot == nullptr || shape_generation == nullptr ||
        !valid_shape_dimensions(kind, dimension_x, dimension_y, dimension_z)) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    const int32_t status = require_world(world_generation);
    if (status != ELISA_PHYSICS_OK) return status;
    PhysicsService& state = physics_service();
    uint32_t free_slot = MAX_SHAPES;
    for (uint32_t index = 0; index < MAX_SHAPES; ++index) {
        if (!state.shapes[index].live) { free_slot = index; break; }
    }
    if (free_slot == MAX_SHAPES) return ELISA_PHYSICS_CAPACITY;
    ShapeSlot& shape = state.shapes[free_slot];
    if (shape.generation == UINT64_MAX) return ELISA_PHYSICS_CAPACITY;
    shape.kind = kind;
    shape.dimension_x = dimension_x;
    shape.dimension_y = dimension_y;
    shape.dimension_z = dimension_z;
    shape.body_references = 0;
    try {
        shape.backend_shape = wi::scene::RigidBodyPhysicsComponent{};
        configure_precomputed_shape(shape);
    } catch (...) {
        shape.backend_shape = wi::scene::RigidBodyPhysicsComponent{};
        return ELISA_PHYSICS_BACKEND_FAILURE;
    }
    ++shape.generation;
    shape.live = true;
    *slot = free_slot;
    *shape_generation = shape.generation;
    return ELISA_PHYSICS_OK;
}

extern "C" int32_t elisa_physics_v1_destroy_shape(uint64_t world_generation,
    uint32_t slot, uint64_t shape_generation) {
    const int32_t status = require_world(world_generation);
    if (status != ELISA_PHYSICS_OK) return status;
    PhysicsService& state = physics_service();
    ShapeSlot* shape = resolve_shape(state, slot, shape_generation);
    if (shape == nullptr) return ELISA_PHYSICS_INVALID_HANDLE;
    if (shape->body_references != 0) return ELISA_PHYSICS_SHAPE_IN_USE;
    shape->backend_shape = wi::scene::RigidBodyPhysicsComponent{};
    shape->dimension_x = 0.0f;
    shape->dimension_y = 0.0f;
    shape->dimension_z = 0.0f;
    shape->live = false;
    return ELISA_PHYSICS_OK;
}

extern "C" int32_t elisa_physics_v1_create_body_with_shape(uint64_t world_generation,
    int32_t kind, float position_x, float position_y, float position_z, float mass,
    int32_t sensor, uint32_t shape_slot, uint64_t shape_generation,
    uint32_t* body_slot, uint64_t* body_generation) {
    if (body_slot == nullptr || body_generation == nullptr ||
        kind < ELISA_PHYSICS_BODY_STATIC || kind > ELISA_PHYSICS_BODY_DYNAMIC ||
        (sensor != SENSOR_DISABLED && sensor != SENSOR_ENABLED) ||
        !std::isfinite(position_x) || !std::isfinite(position_y) || !std::isfinite(position_z) ||
        std::fabs(position_x) > MAX_POSITION || std::fabs(position_y) > MAX_POSITION ||
        std::fabs(position_z) > MAX_POSITION || !std::isfinite(mass) || mass < ZERO_MASS ||
        mass > MAX_MASS || (kind == ELISA_PHYSICS_BODY_DYNAMIC && mass <= ZERO_MASS)) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    const int32_t status = require_world(world_generation);
    if (status != ELISA_PHYSICS_OK) return status;
    PhysicsService& state = physics_service();
    ShapeSlot* shape = resolve_shape(state, shape_slot, shape_generation);
    if (shape == nullptr) return ELISA_PHYSICS_INVALID_HANDLE;
    if (shape->body_references == UINT32_MAX) return ELISA_PHYSICS_CAPACITY;

    uint32_t created_slot = 0;
    uint64_t created_generation = 0;
    const int32_t create_status = elisa_physics_v1_create_body(world_generation, kind,
        shape->kind, position_x, position_y, position_z,
        shape->dimension_x, shape->dimension_y, shape->dimension_z, mass, sensor,
        &created_slot, &created_generation);
    if (create_status != ELISA_PHYSICS_OK) return create_status;

    BodySlot* body = resolve_body(state, created_slot, created_generation);
    wi::scene::MeshComponent* mesh = body != nullptr
        ? state.scene->meshes.GetComponent(body->entity) : nullptr;
    if (body == nullptr || mesh == nullptr) {
        elisa_physics_v1_destroy_body(world_generation, created_slot, created_generation);
        return ELISA_PHYSICS_BACKEND_FAILURE;
    }
    mesh->precomputed_rigidbody_physics_shape = shape->backend_shape;
    body->shape_slot = shape_slot;
    body->shape_generation = shape_generation;
    ++shape->body_references;
    *body_slot = created_slot;
    *body_generation = created_generation;
    return ELISA_PHYSICS_OK;
}
