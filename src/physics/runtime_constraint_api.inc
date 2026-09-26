    private:
        def constraint_kind_code(kind: ConstraintKind) -> i32:
            match kind:
                ConstraintKind.Fixed:
                    Status::CONSTRAINT_FIXED
                ConstraintKind.Point:
                    Status::CONSTRAINT_POINT
                ConstraintKind.Distance:
                    Status::CONSTRAINT_DISTANCE
                ConstraintKind.Hinge:
                    Status::CONSTRAINT_HINGE
                ConstraintKind.Slider:
                    Status::CONSTRAINT_SLIDER

        def native_constraint_create(world_generation: u64,
            description: ConstraintDesc) -> ConstraintHandle error[PhysicsError]:
            raise PhysicsError.StaleWorld if description.body_a.world_generation != world_generation or description.body_b.world_generation != world_generation
            raise PhysicsError.InvalidArgument if description.body_a.slot == description.body_b.slot and description.body_a.generation == description.body_b.generation
            raise PhysicsError.InvalidArgument if not Geometry::vec3_is_finite(description.anchor) or not Geometry::quat_is_finite(description.frame_rotation) or Geometry::quat_length(description.frame_rotation) <= 0.000001
            slot: mutable u32 = 0
            generation: mutable u64 = 0
            status: i32 = elisa_physics_v1_create_constraint(world_generation,
                description.body_a.slot, description.body_a.generation,
                description.body_b.slot, description.body_b.generation,
                constraint_kind_code(description.kind), description.anchor.x,
                description.anchor.y, description.anchor.z,
                description.frame_rotation.x, description.frame_rotation.y,
                description.frame_rotation.z, description.frame_rotation.w,
                description.lower_limit, description.upper_limit,
                description.target_velocity, description.max_motor_force,
                description.break_distance, 1 if description.disable_collision else 0,
                &slot, &generation)
            try check_status(status)
            ConstraintHandle{world_generation: world_generation, slot: slot,
                generation: generation}

    public:
        def service_constraint_create(state: ServiceState&,
            description: ConstraintDesc) -> ConstraintHandle error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not state.live
            try native_constraint_create(state.generation, description)

        def constraint_create(world: World&,
            description: ConstraintDesc) -> ConstraintHandle error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not world.live
            try native_constraint_create(world.generation, description)

        def service_constraint_set_motor(state: ServiceState&,
            constraint: ConstraintHandle, target_velocity: f32,
            max_motor_force: f32) -> void error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not state.live
            raise PhysicsError.StaleWorld if constraint.world_generation != state.generation
            try check_status(elisa_physics_v1_constraint_set_motor(state.generation,
                constraint.slot, constraint.generation, target_velocity, max_motor_force))

        def constraint_set_motor(world: World&,
            constraint: ConstraintHandle, target_velocity: f32,
            max_motor_force: f32) -> void error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not world.live
            raise PhysicsError.StaleWorld if constraint.world_generation != world.generation
            try check_status(elisa_physics_v1_constraint_set_motor(world.generation,
                constraint.slot, constraint.generation, target_velocity, max_motor_force))

        def service_constraint_is_broken(state: ServiceState&,
            constraint: ConstraintHandle) -> bool error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not state.live
            raise PhysicsError.StaleWorld if constraint.world_generation != state.generation
            broken: mutable i32 = 0
            try check_status(elisa_physics_v1_constraint_is_broken(state.generation,
                constraint.slot, constraint.generation, &broken))
            broken != 0

        def constraint_is_broken(world: World&,
            constraint: ConstraintHandle) -> bool error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not world.live
            raise PhysicsError.StaleWorld if constraint.world_generation != world.generation
            broken: mutable i32 = 0
            try check_status(elisa_physics_v1_constraint_is_broken(world.generation,
                constraint.slot, constraint.generation, &broken))
            broken != 0

        def service_constraint_destroy(state: ServiceState&,
            constraint: ConstraintHandle) -> void error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not state.live
            raise PhysicsError.StaleWorld if constraint.world_generation != state.generation
            try check_status(elisa_physics_v1_destroy_constraint(state.generation,
                constraint.slot, constraint.generation))

        def constraint_destroy(world: World&,
            constraint: ConstraintHandle) -> void error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not world.live
            raise PhysicsError.StaleWorld if constraint.world_generation != world.generation
            try check_status(elisa_physics_v1_destroy_constraint(world.generation,
                constraint.slot, constraint.generation))
