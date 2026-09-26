        def physics_constraint_create(session: Session&,
            description: PhysicsRuntime::ConstraintDesc) -> PhysicsRuntime::ConstraintHandle error[RuntimeServicesError, PhysicsRuntime::PhysicsError]:
            raise RuntimeServicesError.SessionStopped if not session.application_live
            raise RuntimeServicesError.PhysicsUnavailable if not PhysicsRuntime::service_is_live(session.physics)
            try PhysicsRuntime::service_constraint_create(session.physics, description)

        def physics_constraint_set_motor(session: Session&,
            constraint: PhysicsRuntime::ConstraintHandle, target_velocity: f32,
            max_motor_force: f32) -> void error[RuntimeServicesError, PhysicsRuntime::PhysicsError]:
            raise RuntimeServicesError.SessionStopped if not session.application_live
            raise RuntimeServicesError.PhysicsUnavailable if not PhysicsRuntime::service_is_live(session.physics)
            try PhysicsRuntime::service_constraint_set_motor(session.physics,
                constraint, target_velocity, max_motor_force)

        def physics_constraint_is_broken(session: Session&,
            constraint: PhysicsRuntime::ConstraintHandle) -> bool error[RuntimeServicesError, PhysicsRuntime::PhysicsError]:
            raise RuntimeServicesError.SessionStopped if not session.application_live
            raise RuntimeServicesError.PhysicsUnavailable if not PhysicsRuntime::service_is_live(session.physics)
            try PhysicsRuntime::service_constraint_is_broken(session.physics, constraint)

        def physics_constraint_destroy(session: Session&,
            constraint: PhysicsRuntime::ConstraintHandle) -> void error[RuntimeServicesError, PhysicsRuntime::PhysicsError]:
            raise RuntimeServicesError.SessionStopped if not session.application_live
            raise RuntimeServicesError.PhysicsUnavailable if not PhysicsRuntime::service_is_live(session.physics)
            try PhysicsRuntime::service_constraint_destroy(session.physics, constraint)
