        struct ShapeHandle:
            private:
                world_generation: u64
                slot: u32
                generation: u64

        struct BodyInstanceDesc:
            kind: BodyKind
            position: Geometry::Vec3
            mass: f32
            sensor: bool

        def shape_create(world: World&, shape: BodyShape,
            dimensions: Geometry::Vec3) -> ShapeHandle error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not world.live
            slot: mutable u32 = 0
            generation: mutable u64 = 0
            status: i32 = elisa_physics_v1_create_shape(world.generation,
                shape_code(shape), dimensions.x, dimensions.y, dimensions.z,
                &slot, &generation)
            try check_status(status)
            ShapeHandle{world_generation: world.generation, slot: slot, generation: generation}

        def shape_destroy(world: World&, shape: ShapeHandle) -> void error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not world.live
            raise PhysicsError.StaleWorld if shape.world_generation != world.generation
            try check_status(elisa_physics_v1_destroy_shape(world.generation,
                shape.slot, shape.generation))

        def body_create_with_shape(world: World&, description: BodyInstanceDesc,
            shape: ShapeHandle) -> BodyHandle error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not world.live
            raise PhysicsError.StaleWorld if shape.world_generation != world.generation
            slot: mutable u32 = 0
            generation: mutable u64 = 0
            status: i32 = elisa_physics_v1_create_body_with_shape(world.generation,
                kind_code(description.kind), description.position.x, description.position.y,
                description.position.z, description.mass, 1 if description.sensor else 0,
                shape.slot, shape.generation, &slot, &generation)
            try check_status(status)
            BodyHandle{world_generation: world.generation, slot: slot, generation: generation}

        def service_shape_create(state: ServiceState&, shape: BodyShape,
            dimensions: Geometry::Vec3) -> ShapeHandle error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not state.live
            slot: mutable u32 = 0
            generation: mutable u64 = 0
            status: i32 = elisa_physics_v1_create_shape(state.generation,
                shape_code(shape), dimensions.x, dimensions.y, dimensions.z,
                &slot, &generation)
            try check_status(status)
            ShapeHandle{world_generation: state.generation, slot: slot, generation: generation}

        def service_shape_destroy(state: ServiceState&, shape: ShapeHandle) -> void error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not state.live
            raise PhysicsError.StaleWorld if shape.world_generation != state.generation
            try check_status(elisa_physics_v1_destroy_shape(state.generation,
                shape.slot, shape.generation))

        def service_body_create_with_shape(state: ServiceState&,
            description: BodyInstanceDesc, shape: ShapeHandle) -> BodyHandle error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not state.live
            raise PhysicsError.StaleWorld if shape.world_generation != state.generation
            slot: mutable u32 = 0
            generation: mutable u64 = 0
            status: i32 = elisa_physics_v1_create_body_with_shape(state.generation,
                kind_code(description.kind), description.position.x, description.position.y,
                description.position.z, description.mass, 1 if description.sensor else 0,
                shape.slot, shape.generation, &slot, &generation)
            try check_status(status)
            BodyHandle{world_generation: state.generation, slot: slot, generation: generation}
