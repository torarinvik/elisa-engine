        def service_body_set_inertia(state: ServiceState&, body: BodyHandle,
            inertia: BodyInertia) -> void error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not state.live
            try native_body_set_inertia(state.generation, body, inertia)

        def service_body_inertia(state: ServiceState&,
            body: BodyHandle) -> BodyInertia error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not state.live
            native_body_inertia(state.generation, body)

        def body_set_inertia(world: World&, body: BodyHandle,
            inertia: BodyInertia) -> void error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not world.live
            try native_body_set_inertia(world.generation, body, inertia)

        def body_inertia(world: World&,
            body: BodyHandle) -> BodyInertia error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not world.live
            native_body_inertia(world.generation, body)
