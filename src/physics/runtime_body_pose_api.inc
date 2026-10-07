        def service_body_pose(state: ServiceState&,
            body: BodyHandle) -> BodyPose error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not state.live
            try native_body_pose(state.generation, body)

        def body_pose(world: World&, body: BodyHandle) -> BodyPose error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not world.live
            try native_body_pose(world.generation, body)

        # Joins copied contact records to a checked body; not a World entity ID.
        def service_body_contact_key(state: ServiceState&, body: BodyHandle) -> u64 error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not state.live
            try native_body_contact_key(state.generation, body)

        def body_contact_key(world: World&, body: BodyHandle) -> u64 error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not world.live
            try native_body_contact_key(world.generation, body)
