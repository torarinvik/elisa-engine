        const MAX_COLLISION_LAYERS: u32 = 32

        def service_set_layer_collision(state: ServiceState&, layer_a: u32,
            layer_b: u32, enabled: bool) -> void error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not state.live
            try check_status(elisa_physics_v1_set_layer_collision(state.generation,
                layer_a, layer_b, 1 if enabled else 0))

        def set_layer_collision(world: World&, layer_a: u32, layer_b: u32,
            enabled: bool) -> void error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not world.live
            raise PhysicsError.InvalidArgument if layer_a >= MAX_COLLISION_LAYERS or layer_b >= MAX_COLLISION_LAYERS
            try check_status(elisa_physics_v1_set_layer_collision(world.generation,
                layer_a, layer_b, 1 if enabled else 0))
