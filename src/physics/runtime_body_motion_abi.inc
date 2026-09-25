        const module MotionLimits:
            MAX_LINEAR_SPEED: f32 = 10000.0
            MAX_IMPULSE_COMPONENT: f32 = 10000000.0

        @callconv(c)
        extern elisa_physics_v1_body_linear_velocity(world_generation: u64,
            slot: u32, body_generation: u64,
            velocity_x: mutable f32&, velocity_y: mutable f32&,
            velocity_z: mutable f32&) -> i32

        @callconv(c)
        extern elisa_physics_v1_body_set_linear_velocity(world_generation: u64,
            slot: u32, body_generation: u64,
            velocity_x: f32, velocity_y: f32, velocity_z: f32) -> i32

        @callconv(c)
        extern elisa_physics_v1_body_apply_impulse(world_generation: u64,
            slot: u32, body_generation: u64,
            impulse_x: f32, impulse_y: f32, impulse_z: f32) -> i32

        def native_body_linear_velocity(world_generation: u64,
            body: BodyHandle) -> Geometry::Vec3 error[PhysicsError]:
            raise PhysicsError.StaleWorld if body.world_generation != world_generation
            x: mutable f32 = 0.0
            y: mutable f32 = 0.0
            z: mutable f32 = 0.0
            status: i32 = elisa_physics_v1_body_linear_velocity(world_generation,
                body.slot, body.generation, &x, &y, &z)
            try check_status(status)
            Geometry::Vec3{x: x, y: y, z: z}

        def native_body_set_linear_velocity(world_generation: u64,
            body: BodyHandle, velocity: Geometry::Vec3) -> void error[PhysicsError]:
            raise PhysicsError.InvalidArgument if not Geometry::vec3_is_finite(velocity) or velocity.x < -MotionLimits::MAX_LINEAR_SPEED or velocity.x > MotionLimits::MAX_LINEAR_SPEED or velocity.y < -MotionLimits::MAX_LINEAR_SPEED or velocity.y > MotionLimits::MAX_LINEAR_SPEED or velocity.z < -MotionLimits::MAX_LINEAR_SPEED or velocity.z > MotionLimits::MAX_LINEAR_SPEED
            raise PhysicsError.StaleWorld if body.world_generation != world_generation
            status: i32 = elisa_physics_v1_body_set_linear_velocity(world_generation,
                body.slot, body.generation, velocity.x, velocity.y, velocity.z)
            try check_status(status)

        def native_body_apply_impulse(world_generation: u64,
            body: BodyHandle, impulse: Geometry::Vec3) -> void error[PhysicsError]:
            raise PhysicsError.InvalidArgument if not Geometry::vec3_is_finite(impulse) or impulse.x < -MotionLimits::MAX_IMPULSE_COMPONENT or impulse.x > MotionLimits::MAX_IMPULSE_COMPONENT or impulse.y < -MotionLimits::MAX_IMPULSE_COMPONENT or impulse.y > MotionLimits::MAX_IMPULSE_COMPONENT or impulse.z < -MotionLimits::MAX_IMPULSE_COMPONENT or impulse.z > MotionLimits::MAX_IMPULSE_COMPONENT
            raise PhysicsError.StaleWorld if body.world_generation != world_generation
            status: i32 = elisa_physics_v1_body_apply_impulse(world_generation,
                body.slot, body.generation, impulse.x, impulse.y, impulse.z)
            try check_status(status)
        include "./runtime_body_pose_abi.inc"
