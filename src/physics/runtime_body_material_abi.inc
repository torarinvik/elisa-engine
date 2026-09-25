        const module MaterialLimits:
            MIN_COEFFICIENT: f32 = 0.0
            MAX_FRICTION: f32 = 10.0
            MAX_RESTITUTION: f32 = 1.0

        def finite(value: f32) -> bool:
            value - value == 0.0

        @callconv(c)
        extern elisa_physics_v1_body_set_material(world_generation: u64,
            slot: u32, body_generation: u64, friction: f32, restitution: f32) -> i32

        def native_body_set_material(world_generation: u64, body: BodyHandle,
            friction: f32, restitution: f32) -> void error[PhysicsError]:
            raise PhysicsError.InvalidArgument if not finite(friction) or friction < MaterialLimits::MIN_COEFFICIENT or friction > MaterialLimits::MAX_FRICTION or not finite(restitution) or restitution < MaterialLimits::MIN_COEFFICIENT or restitution > MaterialLimits::MAX_RESTITUTION
            raise PhysicsError.StaleWorld if body.world_generation != world_generation
            status: i32 = elisa_physics_v1_body_set_material(world_generation,
                body.slot, body.generation, friction, restitution)
            try check_status(status)
