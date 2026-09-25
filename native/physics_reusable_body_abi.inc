namespace {
using namespace elisa_physics_internal;
constexpr double MIN_SHAPE_BODY_ROTATION_LENGTH = 1.0e-6;
}

extern "C" int32_t elisa_physics_v1_create_body_with_shape(uint64_t world_generation,
    int32_t kind, float position_x, float position_y, float position_z,
    float rotation_x, float rotation_y, float rotation_z, float rotation_w, float mass,
    int32_t sensor, uint32_t collision_layer, uint32_t shape_slot, uint64_t shape_generation,
    uint32_t* body_slot, uint64_t* body_generation) {
    if (body_slot == nullptr || body_generation == nullptr ||
        kind < ELISA_PHYSICS_BODY_STATIC || kind > ELISA_PHYSICS_BODY_DYNAMIC ||
        collision_layer >= wi::physics::COLLISION_LAYER_COUNT ||
        (sensor != SENSOR_DISABLED && sensor != SENSOR_ENABLED) ||
        !std::isfinite(position_x) || !std::isfinite(position_y) || !std::isfinite(position_z) ||
        !std::isfinite(rotation_x) || !std::isfinite(rotation_y) ||
        !std::isfinite(rotation_z) || !std::isfinite(rotation_w) ||
        std::fabs(position_x) > MAX_POSITION || std::fabs(position_y) > MAX_POSITION ||
        std::fabs(position_z) > MAX_POSITION || !std::isfinite(mass) || mass < ZERO_MASS ||
        mass > MAX_MASS || (kind == ELISA_PHYSICS_BODY_DYNAMIC && mass <= ZERO_MASS)) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    const double rotation_length = std::sqrt(double(rotation_x) * rotation_x +
        double(rotation_y) * rotation_y + double(rotation_z) * rotation_z +
        double(rotation_w) * rotation_w);
    if (!std::isfinite(rotation_length) ||
        rotation_length <= MIN_SHAPE_BODY_ROTATION_LENGTH) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    const int32_t status = require_world(world_generation);
    if (status != ELISA_PHYSICS_OK) return status;
    PhysicsService& state = physics_service();
    ShapeSlot* shape = resolve_shape(state, shape_slot, shape_generation);
    if (shape == nullptr) return ELISA_PHYSICS_INVALID_HANDLE;
    if (shape->requires_static_body && kind != ELISA_PHYSICS_BODY_STATIC) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    if (shape->body_references == UINT32_MAX) return ELISA_PHYSICS_CAPACITY;

    if (shape->kind == ELISA_PHYSICS_SHAPE_TRIANGLE_MESH ||
        shape->kind == ELISA_PHYSICS_SHAPE_CONVEX_HULL) {
        uint32_t free_slot = MAX_BODIES;
        for (uint32_t index = 0; index < MAX_BODIES; ++index) {
            if (!state.bodies[index].live) { free_slot = index; break; }
        }
        if (free_slot == MAX_BODIES) return ELISA_PHYSICS_CAPACITY;
        BodySlot& body_record = state.bodies[free_slot];
        if (body_record.generation == UINT64_MAX) return ELISA_PHYSICS_CAPACITY;
        const size_t proxy_bytes = shape->mesh_vertices.size() * (2u * sizeof(XMFLOAT3)) +
            shape->mesh_indices.size() * sizeof(uint32_t);
        if (proxy_bytes > MAX_WORLD_MESH_PROXY_BYTES ||
            state.mesh_proxy_geometry_bytes > MAX_WORLD_MESH_PROXY_BYTES - proxy_bytes) {
            return ELISA_PHYSICS_CAPACITY;
        }

        wi::ecs::Entity entity = wi::ecs::INVALID_ENTITY;
        try {
            const std::string name = "elisa_physics_mesh_body_" + std::to_string(free_slot);
            entity = state.scene->Entity_CreateMeshFromData(name, shape->mesh_indices.size(),
                shape->mesh_indices.data(), shape->mesh_vertices.size(), shape->mesh_vertices.data());
            if (entity == wi::ecs::INVALID_ENTITY) return ELISA_PHYSICS_BACKEND_FAILURE;
            wi::scene::TransformComponent* transform = state.scene->transforms.GetComponent(entity);
            wi::scene::MeshComponent* mesh = state.scene->meshes.GetComponent(entity);
            if (transform == nullptr || mesh == nullptr) {
                state.scene->Entity_Remove(entity);
                return ELISA_PHYSICS_BACKEND_FAILURE;
            }
            const ElisaCoordinateProfile profile = elisa_coordinate_profile();
            const ElisaTransformPayload authored{{position_x, position_y, position_z},
                {rotation_x, rotation_y, rotation_z, rotation_w},
                {1.0f, 1.0f, 1.0f}};
            if (!probe::submit_elisa_transform(&profile, &authored, transform)) {
                state.scene->Entity_Remove(entity);
                return ELISA_PHYSICS_INVALID_ARGUMENT;
            }
            transform->UpdateTransform();
            set_body_collision_layer(*state.scene, entity, collision_layer);
            mesh->precomputed_rigidbody_physics_shape = shape->backend_shape;
            wi::scene::RigidBodyPhysicsComponent& rigidbody = state.scene->rigidbodies.Create(entity);
            rigidbody.shape = shape->kind == ELISA_PHYSICS_SHAPE_TRIANGLE_MESH
                ? wi::scene::RigidBodyPhysicsComponent::TRIANGLE_MESH
                : wi::scene::RigidBodyPhysicsComponent::CONVEX_HULL;
            rigidbody.mass = kind == ELISA_PHYSICS_BODY_STATIC ? ZERO_MASS : mass;
            rigidbody.SetKinematic(kind == ELISA_PHYSICS_BODY_KINEMATIC);
            rigidbody.SetSensor(sensor == SENSOR_ENABLED);
            rigidbody.collision_layer = collision_layer;
            rigidbody.collision_filter_subgroup = free_slot;
            if (!wi::physics::RegisterCollisionLayerBody(*state.scene, free_slot, collision_layer)) {
                state.scene->Entity_Remove(entity);
                return ELISA_PHYSICS_BACKEND_FAILURE;
            }
            body_record.entity = entity;
            body_record.shape_slot = shape_slot;
            body_record.shape_generation = shape_generation;
            body_record.mesh_proxy_geometry_bytes = proxy_bytes;
            ++body_record.generation;
            body_record.live = true;
        } catch (...) {
            if (entity != wi::ecs::INVALID_ENTITY) state.scene->Entity_Remove(entity);
            return ELISA_PHYSICS_BACKEND_FAILURE;
        }
        state.mesh_proxy_geometry_bytes += proxy_bytes;
        ++shape->body_references;
        *body_slot = free_slot;
        *body_generation = body_record.generation;
        return ELISA_PHYSICS_OK;
    }

    uint32_t created_slot = 0;
    uint64_t created_generation = 0;
    const bool mesh_shape = shape->kind == ELISA_PHYSICS_SHAPE_CONVEX_HULL ||
        shape->kind == ELISA_PHYSICS_SHAPE_TRIANGLE_MESH ||
        shape->kind == ELISA_PHYSICS_SHAPE_COMPOUND;
    const int32_t proxy_shape = mesh_shape ? ELISA_PHYSICS_SHAPE_BOX : shape->kind;
    const int32_t create_status = elisa_physics_v1_create_body(world_generation, kind,
        proxy_shape, position_x, position_y, position_z,
        rotation_x, rotation_y, rotation_z, rotation_w,
        mesh_shape ? 1.0f : shape->dimension_x,
        mesh_shape ? 1.0f : shape->dimension_y,
        mesh_shape ? 1.0f : shape->dimension_z, mass, sensor, collision_layer,
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
