extern "C" int32_t elisa_physics_v1_create_compound_shape(uint64_t world_generation,
    const uint32_t* child_slots, const uint64_t* child_generations,
    const ElisaPhysicsVec3* child_positions, const ElisaPhysicsQuat* child_rotations,
    uint32_t child_count, uint32_t* slot, uint64_t* shape_generation) {
    static_assert(sizeof(ElisaPhysicsVec3) == sizeof(XMFLOAT3),
        "Elisa compound positions must stay tightly packed XYZ floats");
    static_assert(sizeof(ElisaPhysicsQuat) == sizeof(XMFLOAT4),
        "Elisa compound rotations must stay tightly packed XYZW floats");
    if (slot == nullptr || shape_generation == nullptr || child_slots == nullptr ||
        child_generations == nullptr || child_positions == nullptr || child_rotations == nullptr ||
        child_count == 0 || child_count > MAX_COMPOUND_CHILDREN) {
        return ELISA_PHYSICS_INVALID_ARGUMENT;
    }
    const int32_t status = require_world(world_generation);
    if (status != ELISA_PHYSICS_OK) return status;
    const ElisaCoordinateProfile profile = elisa_coordinate_profile();
    PhysicsService& state = physics_service();
    uint32_t free_slot = MAX_SHAPES;
    for (uint32_t index = 0; index < MAX_SHAPES; ++index) {
        if (!state.shapes[index].live) { free_slot = index; break; }
    }
    if (free_slot == MAX_SHAPES) return ELISA_PHYSICS_CAPACITY;
    ShapeSlot& compound = state.shapes[free_slot];
    if (compound.generation == UINT64_MAX) return ELISA_PHYSICS_CAPACITY;

    std::array<wi::scene::RigidBodyPhysicsComponent, MAX_COMPOUND_CHILDREN> child_components{};
    std::array<wi::physics::RigidBodyShapeChild, MAX_COMPOUND_CHILDREN> backend_children{};
    std::vector<ShapeReference> child_references;
    bool requires_static_body = false;
    try {
        child_references.reserve(child_count);
        for (uint32_t index = 0; index < child_count; ++index) {
            ShapeSlot* child = resolve_shape(state, child_slots[index], child_generations[index]);
            if (child == nullptr) return ELISA_PHYSICS_INVALID_HANDLE;
            if (child->backend_shape.physicsobject == nullptr) return ELISA_PHYSICS_BACKEND_FAILURE;
            const ElisaPhysicsVec3& position = child_positions[index];
            const ElisaPhysicsQuat& rotation = child_rotations[index];
            if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
                !std::isfinite(position.z) || std::fabs(position.x) > MAX_POSITION ||
                std::fabs(position.y) > MAX_POSITION || std::fabs(position.z) > MAX_POSITION ||
                !std::isfinite(rotation.x) || !std::isfinite(rotation.y) ||
                !std::isfinite(rotation.z) || !std::isfinite(rotation.w)) {
                return ELISA_PHYSICS_INVALID_ARGUMENT;
            }
            const double rotation_length = std::sqrt(
                double(rotation.x) * rotation.x + double(rotation.y) * rotation.y +
                double(rotation.z) * rotation.z + double(rotation.w) * rotation.w);
            constexpr double MIN_COMPOUND_ROTATION_LENGTH = 1.0e-6;
            if (!std::isfinite(rotation_length) || rotation_length <= MIN_COMPOUND_ROTATION_LENGTH) {
                return ELISA_PHYSICS_INVALID_ARGUMENT;
            }
            const XMFLOAT3 wicked_position = probe::coordinates::to_wicked(
                position.x, position.y, position.z);
            const XMFLOAT3 scaled_position(wicked_position.x * profile.metres_per_unit,
                wicked_position.y * profile.metres_per_unit,
                wicked_position.z * profile.metres_per_unit);
            if (!probe::coordinates::finite(scaled_position) ||
                std::fabs(scaled_position.x) > MAX_POSITION ||
                std::fabs(scaled_position.y) > MAX_POSITION ||
                std::fabs(scaled_position.z) > MAX_POSITION) {
                return ELISA_PHYSICS_INVALID_ARGUMENT;
            }
            const XMFLOAT4 wicked_rotation = probe::coordinates::to_wicked_rotation(
                float(rotation.x / rotation_length), float(rotation.y / rotation_length),
                float(rotation.z / rotation_length), float(rotation.w / rotation_length));
            child_components[index] = child->backend_shape;
            backend_children[index] = wi::physics::RigidBodyShapeChild{
                &child_components[index],
                scaled_position,
                wicked_rotation
            };
            child_references.push_back(ShapeReference{child_slots[index], child_generations[index]});
            uint32_t repeated_references = 0;
            for (uint32_t previous = 0; previous <= index; ++previous) {
                if (child_slots[previous] == child_slots[index] &&
                    child_generations[previous] == child_generations[index]) {
                    ++repeated_references;
                }
            }
            if (child->compound_references > UINT32_MAX - repeated_references) {
                return ELISA_PHYSICS_CAPACITY;
            }
            requires_static_body = requires_static_body || child->requires_static_body;
        }

        compound.backend_shape = wi::scene::RigidBodyPhysicsComponent{};
        compound.backend_shape.mass = 1.0f;
        compound.backend_shape.shape = wi::scene::RigidBodyPhysicsComponent::BOX;
        if (!wi::physics::CreateCompoundRigidBodyShape(compound.backend_shape,
            backend_children.data(), child_count)) {
            compound.backend_shape = wi::scene::RigidBodyPhysicsComponent{};
            return ELISA_PHYSICS_BACKEND_FAILURE;
        }
    } catch (...) {
        compound.backend_shape = wi::scene::RigidBodyPhysicsComponent{};
        return ELISA_PHYSICS_BACKEND_FAILURE;
    }

    for (const ShapeReference& reference : child_references) {
        ++state.shapes[reference.slot].compound_references;
    }
    compound.kind = ELISA_PHYSICS_SHAPE_COMPOUND;
    compound.dimension_x = compound.dimension_y = compound.dimension_z = 0.0f;
    compound.body_references = 0;
    compound.compound_references = 0;
    compound.requires_static_body = requires_static_body;
    compound.child_shapes = std::move(child_references);
    ++compound.generation;
    compound.live = true;
    *slot = free_slot;
    *shape_generation = compound.generation;
    return ELISA_PHYSICS_OK;
}
