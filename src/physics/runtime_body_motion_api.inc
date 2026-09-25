        def service_body_linear_velocity(state: ServiceState&,
            body: BodyHandle) -> Geometry::Vec3 error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not state.live
            try native_body_linear_velocity(state.generation, body)

        def service_body_set_linear_velocity(state: ServiceState&,
            body: BodyHandle, velocity: Geometry::Vec3) -> void error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not state.live
            try native_body_set_linear_velocity(state.generation, body, velocity)

        def service_body_apply_impulse(state: ServiceState&,
            body: BodyHandle, impulse: Geometry::Vec3) -> void error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not state.live
            try native_body_apply_impulse(state.generation, body, impulse)

        def body_linear_velocity(world: World&,
            body: BodyHandle) -> Geometry::Vec3 error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not world.live
            try native_body_linear_velocity(world.generation, body)

        def body_set_linear_velocity(world: World&,
            body: BodyHandle, velocity: Geometry::Vec3) -> void error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not world.live
            try native_body_set_linear_velocity(world.generation, body, velocity)

        def body_apply_impulse(world: World&,
            body: BodyHandle, impulse: Geometry::Vec3) -> void error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not world.live
            try native_body_apply_impulse(world.generation, body, impulse)
        include "./runtime_body_pose_api.inc"
