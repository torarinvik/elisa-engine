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

bool valid_mesh_shape_kind(int32_t kind) {
    return kind == ELISA_PHYSICS_SHAPE_CONVEX_HULL ||
        kind == ELISA_PHYSICS_SHAPE_TRIANGLE_MESH;
}

bool valid_mesh_shape_scale(float x, float y, float z) {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) &&
        x > ZERO_DIMENSION && y > ZERO_DIMENSION && z > ZERO_DIMENSION &&
        x <= MAX_HALF_EXTENT && y <= MAX_HALF_EXTENT && z <= MAX_HALF_EXTENT;
}

bool valid_mesh_geometry(int32_t kind, const ElisaPhysicsVec3* vertices,
    uint32_t vertex_count, const uint32_t* indices, uint32_t index_count) {
    if (!valid_mesh_shape_kind(kind) || vertices == nullptr || indices == nullptr ||
        vertex_count < (kind == ELISA_PHYSICS_SHAPE_CONVEX_HULL ? 4u : 3u) ||
        vertex_count > MAX_MESH_VERTICES || index_count < 3 ||
        index_count > MAX_MESH_INDICES || index_count % 3 != 0) return false;
    for (uint32_t index = 0; index < vertex_count; ++index) {
        const ElisaPhysicsVec3& vertex = vertices[index];
        if (!std::isfinite(vertex.x) || !std::isfinite(vertex.y) ||
            !std::isfinite(vertex.z) || std::fabs(vertex.x) > MAX_HALF_EXTENT ||
            std::fabs(vertex.y) > MAX_HALF_EXTENT || std::fabs(vertex.z) > MAX_HALF_EXTENT) {
            return false;
        }
    }
    for (uint32_t index = 0; index < index_count; index += 3) {
        const uint32_t a = indices[index], b = indices[index + 1], c = indices[index + 2];
        if (a >= vertex_count || b >= vertex_count || c >= vertex_count ||
            a == b || b == c || a == c) return false;
        const double abx = double(vertices[b].x) - vertices[a].x;
        const double aby = double(vertices[b].y) - vertices[a].y;
        const double abz = double(vertices[b].z) - vertices[a].z;
        const double acx = double(vertices[c].x) - vertices[a].x;
        const double acy = double(vertices[c].y) - vertices[a].y;
        const double acz = double(vertices[c].z) - vertices[a].z;
        const double nx = aby * acz - abz * acy;
        const double ny = abz * acx - abx * acz;
        const double nz = abx * acy - aby * acx;
        if (nx * nx + ny * ny + nz * nz <= 1.0e-20) return false;
    }
    return true;
}

bool cooked_geometry_has_non_degenerate_triangle(const elisa::assets::CookedGeometry& geometry) {
    if (geometry.positions.size() < 9 || geometry.positions.size() % 3 != 0 ||
        geometry.indices.size() < 3 || geometry.indices.size() % 3 != 0) return false;
    const size_t vertex_count = geometry.positions.size() / 3;
    double extent = 0.0;
    for (size_t axis = 0; axis < 3; ++axis) {
        float minimum = geometry.positions[axis];
        float maximum = minimum;
        if (!std::isfinite(minimum)) return false;
        for (size_t vertex = 1; vertex < vertex_count; ++vertex) {
            const float value = geometry.positions[vertex * 3 + axis];
            if (!std::isfinite(value)) return false;
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
        }
        extent = std::max(extent, double(maximum) - minimum);
    }
    if (extent <= 1.0e-6) return false;
    const double area_limit = extent * extent * extent * extent * 1.0e-12;
    for (size_t offset = 0; offset < geometry.indices.size(); offset += 3) {
        const uint32_t a = geometry.indices[offset];
        const uint32_t b = geometry.indices[offset + 1];
        const uint32_t c = geometry.indices[offset + 2];
        if (a >= vertex_count || b >= vertex_count || c >= vertex_count ||
            a == b || b == c || a == c) return false;
        const double abx = geometry.positions[size_t(b) * 3] - geometry.positions[size_t(a) * 3];
        const double aby = geometry.positions[size_t(b) * 3 + 1] - geometry.positions[size_t(a) * 3 + 1];
        const double abz = geometry.positions[size_t(b) * 3 + 2] - geometry.positions[size_t(a) * 3 + 2];
        const double acx = geometry.positions[size_t(c) * 3] - geometry.positions[size_t(a) * 3];
        const double acy = geometry.positions[size_t(c) * 3 + 1] - geometry.positions[size_t(a) * 3 + 1];
        const double acz = geometry.positions[size_t(c) * 3 + 2] - geometry.positions[size_t(a) * 3 + 2];
        const double nx = aby * acz - abz * acy;
        const double ny = abz * acx - abx * acz;
        const double nz = abx * acy - aby * acx;
        if (nx * nx + ny * ny + nz * nz <= area_limit) return false;
    }
    return true;
}

bool cooked_geometry_has_volume(const elisa::assets::CookedGeometry& geometry) {
    if (geometry.positions.size() < 12 || geometry.positions.size() % 3 != 0) return false;
    const size_t vertex_count = geometry.positions.size() / 3;
    double extent = 0.0;
    for (size_t axis = 0; axis < 3; ++axis) {
        float minimum = geometry.positions[axis];
        float maximum = minimum;
        for (size_t vertex = 1; vertex < vertex_count; ++vertex) {
            const float value = geometry.positions[vertex * 3 + axis];
            if (!std::isfinite(value)) return false;
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
        }
        extent = std::max(extent, double(maximum) - minimum);
    }
    if (!(extent > 1.0e-6)) return false;
    const double area_limit = extent * extent * extent * extent * 1.0e-12;
    const double volume_limit = extent * extent * extent * 1.0e-9;
    const double p0[3] = {geometry.positions[0], geometry.positions[1], geometry.positions[2]};
    double v1[3] = {};
    bool found_line = false;
    for (size_t vertex = 1; vertex < vertex_count && !found_line; ++vertex) {
        for (size_t axis = 0; axis < 3; ++axis) {
            v1[axis] = double(geometry.positions[vertex * 3 + axis]) - p0[axis];
        }
        const double length2 = v1[0] * v1[0] + v1[1] * v1[1] + v1[2] * v1[2];
        found_line = length2 > extent * extent * 1.0e-12;
    }
    if (!found_line) return false;
    double normal[3] = {};
    bool found_plane = false;
    for (size_t vertex = 1; vertex < vertex_count && !found_plane; ++vertex) {
        const double v2[3] = {
            double(geometry.positions[vertex * 3]) - p0[0],
            double(geometry.positions[vertex * 3 + 1]) - p0[1],
            double(geometry.positions[vertex * 3 + 2]) - p0[2],
        };
        normal[0] = v1[1] * v2[2] - v1[2] * v2[1];
        normal[1] = v1[2] * v2[0] - v1[0] * v2[2];
        normal[2] = v1[0] * v2[1] - v1[1] * v2[0];
        found_plane = normal[0] * normal[0] + normal[1] * normal[1] +
            normal[2] * normal[2] > area_limit;
    }
    if (!found_plane) return false;
    for (size_t vertex = 1; vertex < vertex_count; ++vertex) {
        const double v3[3] = {
            double(geometry.positions[vertex * 3]) - p0[0],
            double(geometry.positions[vertex * 3 + 1]) - p0[1],
            double(geometry.positions[vertex * 3 + 2]) - p0[2],
        };
        const double signed_volume = normal[0] * v3[0] + normal[1] * v3[1] + normal[2] * v3[2];
        if (std::abs(signed_volume) > volume_limit) return true;
    }
    return false;
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
    shape.compound_references = 0;
    shape.requires_static_body = false;
    std::vector<ShapeReference>().swap(shape.child_shapes);
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
        const ElisaCoordinateProfile profile = elisa_coordinate_profile();
        std::vector<XMFLOAT3> copied_vertices;
        copied_vertices.reserve(vertex_count);
        for (uint32_t index = 0; index < vertex_count; ++index) {
            const ElisaPhysicsVec3& vertex = vertices[index];
            const XMFLOAT3 wicked = probe::coordinates::to_wicked(vertex.x, vertex.y, vertex.z);
            const XMFLOAT3 converted(wicked.x * profile.metres_per_unit,
                wicked.y * profile.metres_per_unit, wicked.z * profile.metres_per_unit);
            if (!probe::coordinates::finite(converted) ||
                std::fabs(converted.x) > MAX_HALF_EXTENT ||
                std::fabs(converted.y) > MAX_HALF_EXTENT ||
                std::fabs(converted.z) > MAX_HALF_EXTENT) {
                return ELISA_PHYSICS_INVALID_ARGUMENT;
            }
            copied_vertices.push_back(converted);
        }
        std::vector<uint32_t> copied_indices;
        copied_indices.reserve(index_count);
        for (uint32_t offset = 0; offset < index_count; offset += 3) {
            copied_indices.push_back(indices[offset]);
            copied_indices.push_back(indices[offset + 1]);
            copied_indices.push_back(indices[offset + 2]);
        }

        wi::scene::MeshComponent source_mesh;
        source_mesh.vertex_positions.reserve(vertex_count);
        for (const XMFLOAT3& vertex : copied_vertices) source_mesh.vertex_positions.push_back(vertex);
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
        shape.dimension_x = shape.dimension_y = shape.dimension_z = 0.0f;
        shape.body_references = 0;
        shape.compound_references = 0;
        shape.requires_static_body = kind == ELISA_PHYSICS_SHAPE_TRIANGLE_MESH;
        std::vector<ShapeReference>().swap(shape.child_shapes);
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

extern "C" int32_t elisa_physics_v1_create_mesh_shape_from_asset(uint64_t world_generation,
    int32_t kind, const char* asset_path, float scale_x, float scale_y, float scale_z,
    uint32_t* slot, uint64_t* shape_generation) {
    if (slot == nullptr || shape_generation == nullptr || !valid_mesh_shape_kind(kind) ||
        !valid_mesh_shape_scale(scale_x, scale_y, scale_z)) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    const int32_t status = require_world(world_generation);
    if (status != ELISA_PHYSICS_OK) return status;

    std::filesystem::path resolved_path;
    elisa::assets::CookedGeometry geometry;
    std::string load_error;
    try {
        if (!elisa::assets::resolve_project_asset_path(asset_path, resolved_path) ||
            !elisa::assets::verify_bundle_dependencies(resolved_path, load_error) ||
            !elisa::assets::load_cooked_geometry_asset(resolved_path.string(), geometry, load_error)) {
            return ELISA_PHYSICS_ASSET_LOAD_FAILED;
        }
    } catch (...) {
        return ELISA_PHYSICS_ASSET_LOAD_FAILED;
    }
    const size_t vertex_count = geometry.positions.size() / 3;
    if (vertex_count > MAX_MESH_VERTICES || geometry.indices.size() > MAX_MESH_INDICES) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    if (!cooked_geometry_has_non_degenerate_triangle(geometry) ||
        !geometry.mesh_placements.empty() || !geometry.skin_bone_names.empty() ||
        !geometry.skin_indices.empty() || !geometry.skin_weights.empty() ||
        !geometry.skin_inverse_bind_matrices.empty() || !geometry.skin_joints.empty() ||
        !geometry.skin_cluster_joints.empty() || !geometry.animation_clips.empty() ||
        !geometry.morph_targets.empty() || !geometry.morph_default_weights.empty()) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    if (kind == ELISA_PHYSICS_SHAPE_CONVEX_HULL && !cooked_geometry_has_volume(geometry)) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    const size_t geometry_bytes = vertex_count * sizeof(XMFLOAT3) +
        geometry.indices.size() * sizeof(uint32_t);
    if (geometry_bytes > MAX_MESH_SHAPE_BYTES) return ELISA_PHYSICS_INVALID_ARGUMENT;

    PhysicsService& state = physics_service();
    uint32_t free_slot = MAX_SHAPES;
    for (uint32_t index = 0; index < MAX_SHAPES; ++index) {
        if (!state.shapes[index].live) { free_slot = index; break; }
    }
    if (free_slot == MAX_SHAPES) return ELISA_PHYSICS_CAPACITY;
    ShapeSlot& shape = state.shapes[free_slot];
    if (shape.generation == UINT64_MAX) return ELISA_PHYSICS_CAPACITY;
    if (state.mesh_geometry_bytes > MAX_WORLD_MESH_SHAPE_BYTES - geometry_bytes) {
        return ELISA_PHYSICS_CAPACITY;
    }

    try {
        const ElisaCoordinateProfile profile = elisa_coordinate_profile();
        std::vector<XMFLOAT3> converted_positions;
        converted_positions.reserve(vertex_count);
        wi::scene::MeshComponent source_mesh;
        source_mesh.vertex_positions.reserve(vertex_count);
        for (size_t index = 0; index < geometry.positions.size(); index += 3) {
            const XMFLOAT3 wicked = probe::coordinates::to_wicked(geometry.positions[index],
                geometry.positions[index + 1], geometry.positions[index + 2]);
            const XMFLOAT3 converted(wicked.x * scale_x * profile.metres_per_unit,
                wicked.y * scale_y * profile.metres_per_unit,
                wicked.z * scale_z * profile.metres_per_unit);
            if (!probe::coordinates::finite(converted) ||
                std::fabs(converted.x) > MAX_HALF_EXTENT ||
                std::fabs(converted.y) > MAX_HALF_EXTENT ||
                std::fabs(converted.z) > MAX_HALF_EXTENT) {
                return ELISA_PHYSICS_INVALID_ARGUMENT;
            }
            converted_positions.push_back(converted);
            source_mesh.vertex_positions.push_back(converted);
        }
        source_mesh.indices.reserve(geometry.indices.size());
        const bool reverse_winding =
            probe::coordinates::winding_reversed(XMFLOAT3(scale_x, scale_y, scale_z));
        for (size_t offset = 0; offset < geometry.indices.size(); offset += 3) {
            source_mesh.indices.push_back(geometry.indices[offset]);
            if (reverse_winding) {
                source_mesh.indices.push_back(geometry.indices[offset + 2]);
                source_mesh.indices.push_back(geometry.indices[offset + 1]);
            } else {
                source_mesh.indices.push_back(geometry.indices[offset + 1]);
                source_mesh.indices.push_back(geometry.indices[offset + 2]);
            }
        }
        wi::scene::MeshComponent::MeshSubset& subset = source_mesh.subsets.emplace_back();
        subset.indexOffset = 0;
        subset.indexCount = uint32_t(source_mesh.indices.size());

        shape.backend_shape = wi::scene::RigidBodyPhysicsComponent{};
        shape.backend_shape.mass = 1.0f;
        shape.kind = kind;
        shape.dimension_x = 0.0f;
        shape.dimension_y = 0.0f;
        shape.dimension_z = 0.0f;
        shape.body_references = 0;
        shape.compound_references = 0;
        shape.requires_static_body = kind == ELISA_PHYSICS_SHAPE_TRIANGLE_MESH;
        std::vector<ShapeReference>().swap(shape.child_shapes);
        shape.mesh_vertices = converted_positions;
        shape.mesh_indices.assign(source_mesh.indices.begin(), source_mesh.indices.end());
        shape.mesh_geometry_bytes = geometry_bytes;
        shape.backend_shape.shape = kind == ELISA_PHYSICS_SHAPE_CONVEX_HULL
            ? wi::scene::RigidBodyPhysicsComponent::CONVEX_HULL
            : wi::scene::RigidBodyPhysicsComponent::TRIANGLE_MESH;
        wi::physics::CreateRigidBodyShape(shape.backend_shape, XMFLOAT3(1.0f, 1.0f, 1.0f),
            &source_mesh);
    } catch (...) {
        shape.backend_shape = wi::scene::RigidBodyPhysicsComponent{};
        std::vector<XMFLOAT3>().swap(shape.mesh_vertices);
        std::vector<uint32_t>().swap(shape.mesh_indices);
        shape.mesh_geometry_bytes = 0;
        return ELISA_PHYSICS_BACKEND_FAILURE;
    }
    if (shape.backend_shape.physicsobject == nullptr) {
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
    if (shape->body_references != 0 || shape->compound_references != 0) {
        return ELISA_PHYSICS_SHAPE_IN_USE;
    }
    for (const ShapeReference& reference : shape->child_shapes) {
        if (reference.slot >= MAX_SHAPES) return ELISA_PHYSICS_BACKEND_FAILURE;
        const ShapeSlot& child = state.shapes[reference.slot];
        if (!child.live || child.generation != reference.generation ||
            child.compound_references == 0) return ELISA_PHYSICS_BACKEND_FAILURE;
    }
    if (shape->mesh_geometry_bytes > state.mesh_geometry_bytes) return ELISA_PHYSICS_BACKEND_FAILURE;
    for (const ShapeReference& reference : shape->child_shapes) {
        --state.shapes[reference.slot].compound_references;
    }
    state.mesh_geometry_bytes -= shape->mesh_geometry_bytes;
    shape->backend_shape = wi::scene::RigidBodyPhysicsComponent{};
    std::vector<XMFLOAT3>().swap(shape->mesh_vertices);
    std::vector<uint32_t>().swap(shape->mesh_indices);
    shape->mesh_geometry_bytes = 0;
    std::vector<ShapeReference>().swap(shape->child_shapes);
    shape->dimension_x = 0.0f;
    shape->dimension_y = 0.0f;
    shape->dimension_z = 0.0f;
    shape->requires_static_body = false;
    shape->live = false;
    return ELISA_PHYSICS_OK;
}

extern "C" int32_t elisa_physics_v1_create_body_with_shape(uint64_t world_generation,
    int32_t kind, float position_x, float position_y, float position_z, float mass,
    int32_t sensor, uint32_t collision_layer, uint32_t shape_slot, uint64_t shape_generation,
    uint32_t* body_slot, uint64_t* body_generation) {
    if (body_slot == nullptr || body_generation == nullptr ||
        kind < ELISA_PHYSICS_BODY_STATIC || kind > ELISA_PHYSICS_BODY_DYNAMIC ||
        collision_layer >= wi::physics::COLLISION_LAYER_COUNT ||
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
