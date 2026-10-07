        @callconv(c)
        extern elisa_physics_v1_body_pose(world_generation: u64, slot: u32,
            body_generation: u64, position_x: mutable f32&,
            position_y: mutable f32&, position_z: mutable f32&,
            rotation_x: mutable f32&, rotation_y: mutable f32&,
            rotation_z: mutable f32&, rotation_w: mutable f32&) -> i32

        def native_body_pose(world_generation: u64,
            body: BodyHandle) -> BodyPose error[PhysicsError]:
            raise PhysicsError.StaleWorld if body.world_generation != world_generation
            position_x: mutable f32 = 0.0
            position_y: mutable f32 = 0.0
            position_z: mutable f32 = 0.0
            rotation_x: mutable f32 = 0.0
            rotation_y: mutable f32 = 0.0
            rotation_z: mutable f32 = 0.0
            rotation_w: mutable f32 = 1.0
            status: i32 = elisa_physics_v1_body_pose(world_generation,
                body.slot, body.generation, &position_x, &position_y,
                &position_z, &rotation_x, &rotation_y, &rotation_z, &rotation_w)
            try check_status(status)
            BodyPose{
                position: Geometry::Vec3{x: position_x, y: position_y, z: position_z},
                rotation: Geometry::Quat{x: rotation_x, y: rotation_y,
                    z: rotation_z, w: rotation_w},
            }

        @callconv(c)
        extern elisa_physics_v1_body_contact_key(world_generation: u64, slot: u32,
            body_generation: u64, contact_key: mutable u64&) -> i32

        def native_body_contact_key(world_generation: u64, body: BodyHandle) -> u64 error[PhysicsError]:
            raise PhysicsError.StaleWorld if body.world_generation != world_generation
            key: mutable u64 = 0
            try check_status(elisa_physics_v1_body_contact_key(world_generation,
                body.slot, body.generation, &key))
            key
