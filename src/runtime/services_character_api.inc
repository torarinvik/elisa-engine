        def physics_character_create(session: Session&,
            description: PhysicsRuntime::CharacterDesc) -> PhysicsRuntime::CharacterHandle error[RuntimeServicesError, PhysicsRuntime::PhysicsError]:
            raise RuntimeServicesError.SessionStopped if not session.application_live
            raise RuntimeServicesError.PhysicsUnavailable if not PhysicsRuntime::service_is_live(session.physics)
            try PhysicsRuntime::service_character_create(session.physics, description)

        def physics_character_move(session: Session&,
            character: PhysicsRuntime::CharacterHandle, direction: Geometry::Vec3,
            movement_speed: f32, jump_speed: f32,
            control_in_air: bool) -> void error[RuntimeServicesError, PhysicsRuntime::PhysicsError]:
            raise RuntimeServicesError.SessionStopped if not session.application_live
            raise RuntimeServicesError.PhysicsUnavailable if not PhysicsRuntime::service_is_live(session.physics)
            try PhysicsRuntime::service_character_move(session.physics, character,
                direction, movement_speed, jump_speed, control_in_air)

        def physics_character_ground(session: Session&,
            character: PhysicsRuntime::CharacterHandle) -> PhysicsRuntime::CharacterGround error[RuntimeServicesError, PhysicsRuntime::PhysicsError]:
            raise RuntimeServicesError.SessionStopped if not session.application_live
            raise RuntimeServicesError.PhysicsUnavailable if not PhysicsRuntime::service_is_live(session.physics)
            try PhysicsRuntime::service_character_ground(session.physics, character)

        def physics_character_set_capsule(session: Session&,
            character: PhysicsRuntime::CharacterHandle, radius: f32,
            cylinder_half_height: f32) -> bool error[RuntimeServicesError, PhysicsRuntime::PhysicsError]:
            raise RuntimeServicesError.SessionStopped if not session.application_live
            raise RuntimeServicesError.PhysicsUnavailable if not PhysicsRuntime::service_is_live(session.physics)
            try PhysicsRuntime::service_character_set_capsule(session.physics,
                character, radius, cylinder_half_height)

        def physics_character_pose(session: Session&,
            character: PhysicsRuntime::CharacterHandle) -> PhysicsRuntime::BodyPose error[RuntimeServicesError, PhysicsRuntime::PhysicsError]:
            raise RuntimeServicesError.SessionStopped if not session.application_live
            raise RuntimeServicesError.PhysicsUnavailable if not PhysicsRuntime::service_is_live(session.physics)
            try PhysicsRuntime::service_character_pose(session.physics, character)

        def physics_character_destroy(session: Session&,
            character: PhysicsRuntime::CharacterHandle) -> void error[RuntimeServicesError, PhysicsRuntime::PhysicsError]:
            raise RuntimeServicesError.SessionStopped if not session.application_live
            raise RuntimeServicesError.PhysicsUnavailable if not PhysicsRuntime::service_is_live(session.physics)
            try PhysicsRuntime::service_character_destroy(session.physics, character)
