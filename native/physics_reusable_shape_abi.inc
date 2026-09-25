namespace {

using namespace elisa_physics_internal;
constexpr double MIN_SHAPE_BODY_ROTATION_LENGTH = 1.0e-6;

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

namespace {

int32_t create_mesh_shape_internal(uint64_t world_generation,
    int32_t kind, const ElisaPhysicsVec3* vertices, uint32_t vertex_count,
    const uint32_t* indices, uint32_t index_count, uint32_t* slot,
    uint64_t* shape_generation, const std::filesystem::path* cache_path = nullptr,
    const std::string* cache_key = nullptr) {
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

        std::filesystem::path array_cache_path;
        std::string array_cache_key;
        bool array_cache_enabled = false;
        try {
            std::filesystem::path project_root;
            array_cache_enabled = cache_path == nullptr && cache_key == nullptr &&
                elisa::assets::project_asset_root(project_root) &&
                elisa::physics::shape_cache::make_array_geometry_identity(project_root,
                    kind, reinterpret_cast<const float*>(copied_vertices.data()),
                    copied_vertices.size(), copied_indices.data(), copied_indices.size(),
                    profile, wi::physics::GetJoltVersionID(), array_cache_path,
                    array_cache_key);
        } catch (...) {
            array_cache_enabled = false;
        }
        const std::filesystem::path* selected_cache_path = array_cache_enabled
            ? &array_cache_path : cache_path;
        const std::string* selected_cache_key = array_cache_enabled
            ? &array_cache_key : cache_key;
        std::vector<uint8_t> cached_shape;
        bool restored_from_cache = false;
        try {
            restored_from_cache = selected_cache_path != nullptr &&
                selected_cache_key != nullptr &&
                elisa::physics::shape_cache::load(*selected_cache_path,
                    *selected_cache_key, kind,
                    wi::physics::GetJoltVersionID(), cached_shape);
        } catch (...) {
            restored_from_cache = false;
        }

        wi::scene::RigidBodyPhysicsComponent cooked_shape{};
        cooked_shape.mass = 1.0f;
        cooked_shape.shape = kind == ELISA_PHYSICS_SHAPE_TRIANGLE_MESH
            ? wi::scene::RigidBodyPhysicsComponent::TRIANGLE_MESH
            : wi::scene::RigidBodyPhysicsComponent::CONVEX_HULL;
        if (restored_from_cache) {
            try {
                restored_from_cache = wi::physics::DeserializeRigidBodyShape(cooked_shape,
                    cached_shape.data(), cached_shape.size());
            } catch (...) {
                restored_from_cache = false;
            }
        }
#if defined(ELISA_PHYSICS_TEST_PROBE)
        if (selected_cache_path != nullptr && selected_cache_key != nullptr) {
            if (restored_from_cache) ++physics_service().shape_cache_hits;
            else ++physics_service().shape_cache_misses;
        }
#endif
        if (!restored_from_cache) {
            wi::scene::MeshComponent source_mesh;
            source_mesh.vertex_positions.reserve(vertex_count);
            for (const XMFLOAT3& vertex : copied_vertices) source_mesh.vertex_positions.push_back(vertex);
            source_mesh.indices.reserve(index_count);
            for (uint32_t index : copied_indices) source_mesh.indices.push_back(index);
            wi::scene::MeshComponent::MeshSubset& subset = source_mesh.subsets.emplace_back();
            subset.indexOffset = 0;
            subset.indexCount = index_count;
            wi::physics::CreateRigidBodyShape(cooked_shape, XMFLOAT3(1.0f, 1.0f, 1.0f),
                &source_mesh);
        }
        if (cooked_shape.physicsobject == nullptr) return ELISA_PHYSICS_BACKEND_FAILURE;

        if (!restored_from_cache &&
            selected_cache_path != nullptr && selected_cache_key != nullptr) {
            try {
                std::vector<uint8_t> serialized_shape;
                if (wi::physics::SerializeRigidBodyShape(cooked_shape, serialized_shape)) {
                    elisa::physics::shape_cache::store(*selected_cache_path,
                        *selected_cache_key, kind,
                        wi::physics::GetJoltVersionID(), serialized_shape);
                }
            } catch (...) {
                // Cache serialization is an optimization; the cooked shape remains usable.
            }
        }

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

} // namespace

extern "C" int32_t elisa_physics_v1_create_mesh_shape(uint64_t world_generation,
    int32_t kind, const ElisaPhysicsVec3* vertices, uint32_t vertex_count,
    const uint32_t* indices, uint32_t index_count, uint32_t* slot,
    uint64_t* shape_generation) {
    return create_mesh_shape_internal(world_generation, kind, vertices, vertex_count,
        indices, index_count, slot, shape_generation);
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
    std::string load_error;
    try {
        if (!elisa::assets::resolve_project_asset_path(asset_path, resolved_path) ||
            !elisa::assets::verify_bundle_dependencies(resolved_path, load_error)) {
            return ELISA_PHYSICS_ASSET_LOAD_FAILED;
        }
    } catch (...) {
        return ELISA_PHYSICS_ASSET_LOAD_FAILED;
    }
    elisa::assets::CookedCollisionGeometry collision;
    try {
        if (elisa::assets::load_cooked_collision_geometry_asset(
                resolved_path.string(), collision, load_error)) {
            if ((kind == ELISA_PHYSICS_SHAPE_CONVEX_HULL &&
                    collision.shape != "convex_hull") ||
                (kind == ELISA_PHYSICS_SHAPE_TRIANGLE_MESH &&
                    collision.shape != "triangle_mesh")) {
                return ELISA_PHYSICS_INVALID_ARGUMENT;
            }
            const size_t vertex_count = collision.positions.size() / 3;
            std::vector<ElisaPhysicsVec3> scaled_positions;
            scaled_positions.reserve(vertex_count);
            for (size_t index = 0; index < collision.positions.size(); index += 3) {
                scaled_positions.push_back(ElisaPhysicsVec3{
                    collision.positions[index] * scale_x,
                    collision.positions[index + 1] * scale_y,
                    collision.positions[index + 2] * scale_z,
                });
            }
            const ElisaCoordinateProfile profile = elisa_coordinate_profile();
            const auto cache = elisa::physics::shape_cache::make_identity(resolved_path,
                kind, scale_x, scale_y, scale_z, profile, wi::physics::GetJoltVersionID());
            return create_mesh_shape_internal(world_generation, kind,
                scaled_positions.data(), static_cast<uint32_t>(scaled_positions.size()),
                collision.indices.data(), static_cast<uint32_t>(collision.indices.size()),
                slot, shape_generation, cache.enabled ? &cache.path : nullptr,
                cache.enabled ? &cache.key : nullptr);
        }
    } catch (...) {
        return ELISA_PHYSICS_ASSET_LOAD_FAILED;
    }
    elisa::assets::CookedGeometry geometry;
    try {
        if (!elisa::assets::load_cooked_geometry_asset(resolved_path.string(), geometry, load_error)) {
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

        const auto cache = elisa::physics::shape_cache::make_identity(resolved_path,
            kind, scale_x, scale_y, scale_z, profile, wi::physics::GetJoltVersionID());
        std::vector<uint8_t> cached_shape;
        bool restored_from_cache = false;
        try {
            restored_from_cache = cache.enabled &&
                elisa::physics::shape_cache::load(cache.path, cache.key, kind,
                    wi::physics::GetJoltVersionID(), cached_shape);
        } catch (...) {
            restored_from_cache = false;
        }

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
        if (restored_from_cache) {
            try {
                restored_from_cache = wi::physics::DeserializeRigidBodyShape(
                    shape.backend_shape, cached_shape.data(), cached_shape.size());
            } catch (...) {
                restored_from_cache = false;
            }
        }
#if defined(ELISA_PHYSICS_TEST_PROBE)
        if (cache.enabled) {
            if (restored_from_cache) ++physics_service().shape_cache_hits;
            else ++physics_service().shape_cache_misses;
        }
#endif
        if (!restored_from_cache) {
            wi::physics::CreateRigidBodyShape(shape.backend_shape,
                XMFLOAT3(1.0f, 1.0f, 1.0f), &source_mesh);
        }
        if (shape.backend_shape.physicsobject != nullptr && !restored_from_cache &&
            cache.enabled) {
            try {
                std::vector<uint8_t> serialized_shape;
                if (wi::physics::SerializeRigidBodyShape(shape.backend_shape, serialized_shape)) {
                    elisa::physics::shape_cache::store(cache.path, cache.key, kind,
                        wi::physics::GetJoltVersionID(), serialized_shape);
                }
            } catch (...) {
                // Cache serialization is an optimization; the cooked shape remains usable.
            }
        }
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
