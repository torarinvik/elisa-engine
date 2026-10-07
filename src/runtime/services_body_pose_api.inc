        def physics_body_pose(session: Session&,
            body: PhysicsRuntime::BodyHandle) -> PhysicsRuntime::BodyPose error[RuntimeServicesError, PhysicsRuntime::PhysicsError]:
            raise RuntimeServicesError.SessionStopped if not session.application_live
            raise RuntimeServicesError.PhysicsUnavailable if not PhysicsRuntime::service_is_live(session.physics)
            try PhysicsRuntime::service_body_pose(session.physics, body)

        def physics_body_contact_key(session: Session&, body: PhysicsRuntime::BodyHandle) -> u64 error[RuntimeServicesError, PhysicsRuntime::PhysicsError]:
            raise RuntimeServicesError.SessionStopped if not session.application_live
            raise RuntimeServicesError.PhysicsUnavailable if not PhysicsRuntime::service_is_live(session.physics)
            try PhysicsRuntime::service_body_contact_key(session.physics, body)
