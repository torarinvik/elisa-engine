        # R06: world bounds of a body's collision shape, in Elisa coordinates.
        @callconv(c)
        extern elisa_physics_v1_body_bounds(world_generation: u64, slot: u32,
            body_generation: u64, min_x: mutable f32&, min_y: mutable f32&,
            min_z: mutable f32&, max_x: mutable f32&, max_y: mutable f32&,
            max_z: mutable f32&) -> i32

        def native_body_bounds(world_generation: u64,
            body: BodyHandle) -> Geometry::Bounds3 error[PhysicsError]:
            raise PhysicsError.StaleWorld if body.world_generation != world_generation
            min_x: mutable f32 = 0.0
            min_y: mutable f32 = 0.0
            min_z: mutable f32 = 0.0
            max_x: mutable f32 = 0.0
            max_y: mutable f32 = 0.0
            max_z: mutable f32 = 0.0
            status: i32 = elisa_physics_v1_body_bounds(world_generation, body.slot,
                body.generation, &min_x, &min_y, &min_z, &max_x, &max_y, &max_z)
            try check_status(status)
            Geometry::Bounds3{
                minimum: Geometry::Vec3{x: min_x, y: min_y, z: min_z},
                maximum: Geometry::Vec3{x: max_x, y: max_y, z: max_z},
            }
