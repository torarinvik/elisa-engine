        @callconv(c)
        extern elisa_physics_v1_body_set_inertia(world_generation: u64,
            slot: u32, body_generation: u64,
            moment_x: f32, moment_y: f32, moment_z: f32,
            rotation_x: f32, rotation_y: f32, rotation_z: f32,
            rotation_w: f32) -> i32

        @callconv(c)
        extern elisa_physics_v1_body_inertia(world_generation: u64,
            slot: u32, body_generation: u64,
            moment_x: mutable f32&, moment_y: mutable f32&,
            moment_z: mutable f32&, rotation_x: mutable f32&,
            rotation_y: mutable f32&, rotation_z: mutable f32&,
            rotation_w: mutable f32&) -> i32

        def native_body_set_inertia(world_generation: u64, body: BodyHandle,
            inertia: BodyInertia) -> void error[PhysicsError]:
            raise PhysicsError.StaleWorld if body.world_generation != world_generation
            try check_status(elisa_physics_v1_body_set_inertia(world_generation,
                body.slot, body.generation, inertia.principal_moments.x,
                inertia.principal_moments.y, inertia.principal_moments.z,
                inertia.principal_rotation.x, inertia.principal_rotation.y,
                inertia.principal_rotation.z, inertia.principal_rotation.w))

        def native_body_inertia(world_generation: u64,
            body: BodyHandle) -> BodyInertia error[PhysicsError]:
            raise PhysicsError.StaleWorld if body.world_generation != world_generation
            moment_x: mutable f32 = 0.0
            moment_y: mutable f32 = 0.0
            moment_z: mutable f32 = 0.0
            rotation_x: mutable f32 = 0.0
            rotation_y: mutable f32 = 0.0
            rotation_z: mutable f32 = 0.0
            rotation_w: mutable f32 = 1.0
            try check_status(elisa_physics_v1_body_inertia(world_generation,
                body.slot, body.generation, &moment_x, &moment_y, &moment_z,
                &rotation_x, &rotation_y, &rotation_z, &rotation_w))
            BodyInertia(
                Geometry::Vec3{x: moment_x, y: moment_y, z: moment_z},
                Geometry::Quat{x: rotation_x, y: rotation_y, z: rotation_z, w: rotation_w})
