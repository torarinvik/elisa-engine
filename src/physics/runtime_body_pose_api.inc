        def service_body_pose(state: ServiceState&,
            body: BodyHandle) -> BodyPose error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not state.live
            try native_body_pose(state.generation, body)

        def body_pose(world: World&, body: BodyHandle) -> BodyPose error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not world.live
            try native_body_pose(world.generation, body)
