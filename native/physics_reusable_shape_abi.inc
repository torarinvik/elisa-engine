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

bool valid_mesh_geometry(int32_t kind, const ElisaPhysicsVec3* vertices,
    uint32_t vertex_count, const uint32_t* indices, uint32_t index_count) {
    if ((kind != ELISA_PHYSICS_SHAPE_TRIANGLE_MESH &&
            kind != ELISA_PHYSICS_SHAPE_CONVEX_HULL) || vertices == nullptr ||
        indices == nullptr || vertex_count < (kind == ELISA_PHYSICS_SHAPE_CONVEX_HULL ? 4u : 3u) ||
        vertex_count > MAX_MESH_VERTICES || index_count < 3 ||
        index_count > MAX_MESH_INDICES || index_count % 3 != 0) {
        return false;
    }
    for (uint32_t index = 0; index < vertex_count; ++index) {
        const ElisaPhysicsVec3& vertex = vertices[index];
        if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) ||
            !std::isfinite(vertex.z) || std::fabs(vertex.x) > MAX_HALF_EXTENT ||
            std::fabs(vertex.y) > MAX_HALF_EXTENT || std::fabs(vertex.z) > MAX_HALF_EXTENT) {
            return false;
        }
    }
    for (uint32_t index = 0; index < index_count; index += 3) {
        const uint32_t a = indices[index + 0];
        const uint32_t b = indices[index + 1];
        const uint32_t c = indices[index + 2];
        if (a >= vertex_count || b >= vertex_count || c >= vertex_count ||
            a == b || b == c || a == c) {
            return false;
        }
        const ElisaPhysicsVec3 ab{vertices[b].x - vertices[a].x,
            vertices[b].y - vertices[a].y, vertices[b].z - vertices[a].z};
        const ElisaPhysicsVec3 ac{vertices[c].x - vertices[a].x,
            vertices[c].y - vertices[a].y, vertices[c].z - vertices[a].z};
        const float cross_x = ab.y * ac.z - ab.z * ac.y;
        const float cross_y = ab.z * ac.x - ab.x * ac.z;
        const float cross_z = ab.x * ac.y - ab.y * ac.x;
        if (cross_x * cross_x + cross_y * cross_y + cross_z * cross_z <= 1.0e-12f) {
            return false;
        }
    }
    return true;
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

extern "C" int32_t elisa_physics_v1_create_mesh_shape(uint64_t world_generation,
    int32_t kind, const ElisaPhysicsVec3* vertices, uint32_t vertex_count,
    const uint32_t* indices, uint32_t index_count, uint32_t* slot,
    uint64_t* shape_generation) {
    static_assert(sizeof(ElisaPhysicsVec3) == sizeof(XMFLOAT3),
        "Elisa mesh positions must stay tightly packed XYZ floats");
    if (slot == nullptr || shape_generation == nullptr ||
        !valid_mesh_geometry(kind, vertices, vertex_count, indices, index_count)) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    const size_t geometry_bytes = size_t(vertex_count) * sizeof(XMFLOAT3) +
        size_t(index_count) * sizeof(uint32_t);
    if (geometry_bytes > MAX_MESH_SHAPE_BYTES) return ELISA_PHYSICS_INVALID_ARGUMENT;
    const int32_t status = require_world(world_generation);
    if (status != ELISA_PHYSICS_OK) return status;
    PhysicsService& state = physics_service();
    if (state.mesh_geometry_bytes > MAX_WORLD_MESH_SHAPE_BYTES - geometry_bytes) {
        return ELISA_PHYSICS_CAPACITY;
    }
    uint32_t free_slot = MAX_SHAPES;
    for (uint32_t index = 0; index < MAX_SHAPES; ++index) {
        if (!state.shapes[index].live) { free_slot = index; break; }
    }
    if (free_slot == MAX_SHAPES) return ELISA_PHYSICS_CAPACITY;
    ShapeSlot& shape = state.shapes[free_slot];
    if (shape.generation == UINT64_MAX) return ELISA_PHYSICS_CAPACITY;

    try {
        std::vector<XMFLOAT3> copied_vertices;
        copied_vertices.reserve(vertex_count);
        const ElisaCoordinateProfile profile = elisa_coordinate_profile();
        for (uint32_t index = 0; index < vertex_count; ++index) {
            const ElisaPhysicsVec3& vertex = vertices[index];
            copied_vertices.emplace_back(-vertex.x * profile.metres_per_unit,
                vertex.y * profile.metres_per_unit, vertex.z * profile.metres_per_unit);
        }
        std::vector<uint32_t> copied_indices(indices, indices + index_count);

        wi::scene::MeshComponent source_mesh;
        source_mesh.vertex_positions.reserve(vertex_count);
        for (const XMFLOAT3& vertex : copied_vertices) {
            source_mesh.vertex_positions.push_back(vertex);
        }
        source_mesh.indices.reserve(index_count);
        for (uint32_t index : copied_indices) source_mesh.indices.push_back(index);
        wi::scene::MeshComponent::MeshSubset& subset = source_mesh.subsets.emplace_back();
        subset.indexOffset = 0;
        subset.indexCount = index_count;

        wi::scene::RigidBodyPhysicsComponent cooked_shape{};
        cooked_shape.mass = 1.0f;
        cooked_shape.shape = kind == ELISA_PHYSICS_SHAPE_TRIANGLE_MESH
            ? wi::scene::RigidBodyPhysicsComponent::TRIANGLE_MESH
            : wi::scene::RigidBodyPhysicsComponent::CONVEX_HULL;
        wi::physics::CreateRigidBodyShape(cooked_shape, XMFLOAT3(1.0f, 1.0f, 1.0f),
            &source_mesh);
        if (cooked_shape.physicsobject == nullptr) return ELISA_PHYSICS_BACKEND_FAILURE;

        shape.backend_shape = cooked_shape;
        shape.mesh_vertices = std::move(copied_vertices);
        shape.mesh_indices = std::move(copied_indices);
        shape.mesh_geometry_bytes = geometry_bytes;
        shape.kind = kind;
        shape.dimension_x = 0.0f;
        shape.dimension_y = 0.0f;
        shape.dimension_z = 0.0f;
        shape.body_references = 0;
    } catch (...) {
        shape.backend_shape = wi::scene::RigidBodyPhysicsComponent{};
        std::vector<XMFLOAT3>().swap(shape.mesh_vertices);
        std::vector<uint32_t>().swap(shape.mesh_indices);
        shape.mesh_geometry_bytes = 0;
        return ELISA_PHYSICS_BACKEND_FAILURE;
    }
    state.mesh_geometry_bytes += geometry_bytes;
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
    if (shape->mesh_geometry_bytes > state.mesh_geometry_bytes) return ELISA_PHYSICS_BACKEND_FAILURE;
    state.mesh_geometry_bytes -= shape->mesh_geometry_bytes;
    shape->backend_shape = wi::scene::RigidBodyPhysicsComponent{};
    std::vector<XMFLOAT3>().swap(shape->mesh_vertices);
    std::vector<uint32_t>().swap(shape->mesh_indices);
    shape->mesh_geometry_bytes = 0;
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

    if (shape->kind == ELISA_PHYSICS_SHAPE_TRIANGLE_MESH ||
        shape->kind == ELISA_PHYSICS_SHAPE_CONVEX_HULL) {
        if ((shape->kind == ELISA_PHYSICS_SHAPE_TRIANGLE_MESH &&
                kind != ELISA_PHYSICS_BODY_STATIC) ||
            !std::isfinite(position_x) || !std::isfinite(position_y) ||
            !std::isfinite(position_z) || std::fabs(position_x) > MAX_POSITION ||
            std::fabs(position_y) > MAX_POSITION || std::fabs(position_z) > MAX_POSITION ||
            !std::isfinite(mass) || mass < ZERO_MASS || mass > MAX_MASS ||
            (kind == ELISA_PHYSICS_BODY_DYNAMIC && mass <= ZERO_MASS)) {
            return ELISA_PHYSICS_INVALID_ARGUMENT;
        }
        uint32_t free_slot = MAX_BODIES;
        for (uint32_t index = 0; index < MAX_BODIES; ++index) {
            if (!state.bodies[index].live) { free_slot = index; break; }
        }
        if (free_slot == MAX_BODIES) return ELISA_PHYSICS_CAPACITY;
        BodySlot& body_slot_record = state.bodies[free_slot];
        if (body_slot_record.generation == UINT64_MAX) return ELISA_PHYSICS_CAPACITY;
        const size_t mesh_proxy_bytes = shape->mesh_vertices.size() *
            (2u * sizeof(XMFLOAT3)) + shape->mesh_indices.size() * sizeof(uint32_t);
        if (mesh_proxy_bytes > MAX_WORLD_MESH_PROXY_BYTES ||
            state.mesh_proxy_geometry_bytes > MAX_WORLD_MESH_PROXY_BYTES - mesh_proxy_bytes) {
            return ELISA_PHYSICS_CAPACITY;
        }

        wi::ecs::Entity entity = wi::ecs::INVALID_ENTITY;
        try {
            const std::string body_name = "elisa_physics_mesh_body_" + std::to_string(free_slot);
            entity = state.scene->Entity_CreateMeshFromData(body_name,
                shape->mesh_indices.size(), shape->mesh_indices.data(),
                shape->mesh_vertices.size(), shape->mesh_vertices.data());
            if (entity == wi::ecs::INVALID_ENTITY) return ELISA_PHYSICS_BACKEND_FAILURE;
            wi::scene::TransformComponent* transform = state.scene->transforms.GetComponent(entity);
            wi::scene::MeshComponent* mesh = state.scene->meshes.GetComponent(entity);
            if (transform == nullptr || mesh == nullptr) {
                state.scene->Entity_Remove(entity);
                return ELISA_PHYSICS_BACKEND_FAILURE;
            }
            const ElisaCoordinateProfile profile = elisa_coordinate_profile();
            const ElisaTransformPayload authored{{position_x, position_y, position_z},
                {0.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f}};
            if (!probe::submit_elisa_transform(&profile, &authored, transform)) {
                state.scene->Entity_Remove(entity);
                return ELISA_PHYSICS_INVALID_ARGUMENT;
            }
            transform->UpdateTransform();
            mesh->precomputed_rigidbody_physics_shape = shape->backend_shape;
            wi::scene::RigidBodyPhysicsComponent& rigidbody = state.scene->rigidbodies.Create(entity);
            rigidbody.shape = shape->kind == ELISA_PHYSICS_SHAPE_TRIANGLE_MESH
                ? wi::scene::RigidBodyPhysicsComponent::TRIANGLE_MESH
                : wi::scene::RigidBodyPhysicsComponent::CONVEX_HULL;
            rigidbody.mass = kind == ELISA_PHYSICS_BODY_STATIC ? ZERO_MASS : mass;
            rigidbody.SetKinematic(kind == ELISA_PHYSICS_BODY_KINEMATIC);
            rigidbody.SetSensor(sensor == SENSOR_ENABLED);
            body_slot_record.entity = entity;
            body_slot_record.shape_slot = shape_slot;
            body_slot_record.shape_generation = shape_generation;
            body_slot_record.mesh_proxy_geometry_bytes = mesh_proxy_bytes;
            ++body_slot_record.generation;
            body_slot_record.live = true;
        } catch (...) {
            if (entity != wi::ecs::INVALID_ENTITY) state.scene->Entity_Remove(entity);
            return ELISA_PHYSICS_BACKEND_FAILURE;
        }
        state.mesh_proxy_geometry_bytes += mesh_proxy_bytes;
        ++shape->body_references;
        *body_slot = free_slot;
        *body_generation = body_slot_record.generation;
        return ELISA_PHYSICS_OK;
    }

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
