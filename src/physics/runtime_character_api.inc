        enum CharacterGroundState:
            OnGround
            OnSteepGround
            Unsupported
            InAir

        struct CharacterGround:
            state: CharacterGroundState
            position: Geometry::Vec3
            normal: Geometry::Vec3
            velocity: Geometry::Vec3

        struct CharacterDesc:
            position: Geometry::Vec3
            rotation: Geometry::Quat
            radius: f32
            cylinder_half_height: f32
            mass: f32
            max_slope_angle: f32
            gravity_factor: f32
            collision_layer: u32

        def CharacterDesc(position: Geometry::Vec3, rotation: Geometry::Quat,
            radius: f32, cylinder_half_height: f32, mass: f32,
            max_slope_angle: f32, gravity_factor: f32,
            collision_layer: u32) -> CharacterDesc:
            CharacterDesc{
                position: position,
                rotation: rotation,
                radius: radius,
                cylinder_half_height: cylinder_half_height,
                mass: mass,
                max_slope_angle: max_slope_angle,
                gravity_factor: gravity_factor,
                collision_layer: collision_layer,
            }

        struct CharacterHandle:
            private:
                body: BodyHandle

    private:
        const module CharacterLimits:
            MAX_SPEED: f32 = 1000.0

        def native_character_create(world_generation: u64,
            description: CharacterDesc) -> CharacterHandle error[PhysicsError]:
            raise PhysicsError.InvalidArgument if not Geometry::vec3_is_finite(description.position) or not Geometry::quat_is_finite(description.rotation)
            slot: mutable u32 = 0
            generation: mutable u64 = 0
            status: i32 = elisa_physics_v1_create_character(world_generation,
                description.position.x, description.position.y, description.position.z,
                description.rotation.x, description.rotation.y,
                description.rotation.z, description.rotation.w,
                description.radius, description.cylinder_half_height, description.mass,
                description.max_slope_angle, description.gravity_factor,
                description.collision_layer, &slot, &generation)
            try check_status(status)
            CharacterHandle{body: BodyHandle{
                world_generation: world_generation, slot: slot, generation: generation}}

        def native_character_move(world_generation: u64,
            character: CharacterHandle, direction: Geometry::Vec3,
            movement_speed: f32, jump_speed: f32,
            control_in_air: bool) -> void error[PhysicsError]:
            raise PhysicsError.StaleWorld if character.body.world_generation != world_generation
            raise PhysicsError.InvalidArgument if not Geometry::vec3_is_finite(direction) or direction.y != 0.0 or not finite(movement_speed) or movement_speed < 0.0 or movement_speed > CharacterLimits::MAX_SPEED or not finite(jump_speed) or jump_speed < 0.0 or jump_speed > CharacterLimits::MAX_SPEED
            status: i32 = elisa_physics_v1_character_move(world_generation,
                character.body.slot, character.body.generation,
                direction.x, direction.y, direction.z, movement_speed, jump_speed,
                1 if control_in_air else 0)
            try check_status(status)

        def native_character_ground(world_generation: u64,
            character: CharacterHandle) -> CharacterGround error[PhysicsError]:
            raise PhysicsError.StaleWorld if character.body.world_generation != world_generation
            state: mutable i32 = 0
            ground_x: mutable f32 = 0.0
            ground_y: mutable f32 = 0.0
            ground_z: mutable f32 = 0.0
            normal_x: mutable f32 = 0.0
            normal_y: mutable f32 = 0.0
            normal_z: mutable f32 = 0.0
            velocity_x: mutable f32 = 0.0
            velocity_y: mutable f32 = 0.0
            velocity_z: mutable f32 = 0.0
            status: i32 = elisa_physics_v1_character_info(world_generation,
                character.body.slot, character.body.generation, &state,
                &ground_x, &ground_y, &ground_z,
                &normal_x, &normal_y, &normal_z,
                &velocity_x, &velocity_y, &velocity_z)
            try check_status(status)
            ground_state: mutable CharacterGroundState = CharacterGroundState.InAir
            if state == 0:
                ground_state <- CharacterGroundState.OnGround
            if state == 1:
                ground_state <- CharacterGroundState.OnSteepGround
            if state == 2:
                ground_state <- CharacterGroundState.Unsupported
            CharacterGround{
                state: ground_state,
                position: Geometry::Vec3{x: ground_x, y: ground_y, z: ground_z},
                normal: Geometry::Vec3{x: normal_x, y: normal_y, z: normal_z},
                velocity: Geometry::Vec3{x: velocity_x, y: velocity_y, z: velocity_z},
            }

        def native_character_set_capsule(world_generation: u64,
            character: CharacterHandle, radius: f32,
            cylinder_half_height: f32) -> bool error[PhysicsError]:
            raise PhysicsError.StaleWorld if character.body.world_generation != world_generation
            changed: mutable i32 = 0
            status: i32 = elisa_physics_v1_character_set_capsule(world_generation,
                character.body.slot, character.body.generation,
                radius, cylinder_half_height, &changed)
            try check_status(status)
            changed != 0

    public:
        def service_character_create(state: ServiceState&,
            description: CharacterDesc) -> CharacterHandle error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not state.live
            try native_character_create(state.generation, description)

        def character_create(world: World&,
            description: CharacterDesc) -> CharacterHandle error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not world.live
            try native_character_create(world.generation, description)

        def service_character_move(state: ServiceState&,
            character: CharacterHandle, direction: Geometry::Vec3,
            movement_speed: f32, jump_speed: f32,
            control_in_air: bool) -> void error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not state.live
            try native_character_move(state.generation, character, direction,
                movement_speed, jump_speed, control_in_air)

        def character_move(world: World&, character: CharacterHandle,
            direction: Geometry::Vec3, movement_speed: f32,
            jump_speed: f32, control_in_air: bool) -> void error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not world.live
            try native_character_move(world.generation, character, direction,
                movement_speed, jump_speed, control_in_air)

        def service_character_ground(state: ServiceState&,
            character: CharacterHandle) -> CharacterGround error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not state.live
            try native_character_ground(state.generation, character)

        def character_ground(world: World&,
            character: CharacterHandle) -> CharacterGround error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not world.live
            try native_character_ground(world.generation, character)

        def service_character_set_capsule(state: ServiceState&,
            character: CharacterHandle, radius: f32,
            cylinder_half_height: f32) -> bool error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not state.live
            try native_character_set_capsule(state.generation, character,
                radius, cylinder_half_height)

        def character_set_capsule(world: World&, character: CharacterHandle,
            radius: f32, cylinder_half_height: f32) -> bool error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not world.live
            try native_character_set_capsule(world.generation, character,
                radius, cylinder_half_height)

        def service_character_pose(state: ServiceState&,
            character: CharacterHandle) -> BodyPose error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not state.live
            try native_body_pose(state.generation, character.body)

        def character_pose(world: World&,
            character: CharacterHandle) -> BodyPose error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not world.live
            try native_body_pose(world.generation, character.body)

        def service_character_destroy(state: ServiceState&,
            character: CharacterHandle) -> void error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not state.live
            try service_body_destroy(state, character.body)

        def character_destroy(world: World&,
            character: CharacterHandle) -> void error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not world.live
            try body_destroy(world, character.body)
