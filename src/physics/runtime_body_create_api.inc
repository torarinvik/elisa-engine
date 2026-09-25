        def service_body_create(state: ServiceState&,
            description: BodyDesc) -> BodyHandle error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not state.live
            raise PhysicsError.InvalidArgument if not center_of_mass_offset_valid(description.center_of_mass_offset)
            slot: mutable u32 = 0
            generation: mutable u64 = 0
            status: i32 = elisa_physics_v1_create_body(state.generation,
                kind_code(description.kind), shape_code(description.shape),
                description.position.x, description.position.y, description.position.z,
                description.rotation.x, description.rotation.y,
                description.rotation.z, description.rotation.w,
                description.center_of_mass_offset.x,
                description.center_of_mass_offset.y,
                description.center_of_mass_offset.z,
                description.dimensions.x, description.dimensions.y,
                description.dimensions.z, description.mass, 1 if description.sensor else 0,
                description.collision_layer, &slot, &generation)
            try check_status(status)
            BodyHandle{world_generation: state.generation, slot: slot, generation: generation}

        def body_create(world: World&, description: BodyDesc) -> BodyHandle error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not world.live
            raise PhysicsError.InvalidArgument if not center_of_mass_offset_valid(description.center_of_mass_offset)
            slot: mutable u32 = 0
            generation: mutable u64 = 0
            status: i32 = elisa_physics_v1_create_body(world.generation,
                kind_code(description.kind), shape_code(description.shape),
                description.position.x, description.position.y, description.position.z,
                description.rotation.x, description.rotation.y,
                description.rotation.z, description.rotation.w,
                description.center_of_mass_offset.x,
                description.center_of_mass_offset.y,
                description.center_of_mass_offset.z,
                description.dimensions.x, description.dimensions.y,
                description.dimensions.z, description.mass, 1 if description.sensor else 0,
                description.collision_layer, &slot, &generation)
            try check_status(status)
            BodyHandle{world_generation: world.generation, slot: slot, generation: generation}
