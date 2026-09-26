        def physics_grab_begin(session: Session&, ray: Geometry::Ray3,
            kind: PhysicsRuntime::GrabKind, break_distance: f32) -> PhysicsRuntime::GrabHandle error[RuntimeServicesError, PhysicsRuntime::PhysicsError]:
            raise RuntimeServicesError.SessionStopped if not session.application_live
            raise RuntimeServicesError.PhysicsUnavailable if not PhysicsRuntime::service_is_live(session.physics)
            try PhysicsRuntime::service_grab_begin(session.physics, ray, kind, break_distance)

        def physics_grab_update(session: Session&, grab: PhysicsRuntime::GrabHandle,
            ray: Geometry::Ray3) -> void error[RuntimeServicesError, PhysicsRuntime::PhysicsError]:
            raise RuntimeServicesError.SessionStopped if not session.application_live
            raise RuntimeServicesError.PhysicsUnavailable if not PhysicsRuntime::service_is_live(session.physics)
            try PhysicsRuntime::service_grab_update(session.physics, grab, ray)

        def physics_grab_end(session: Session&, grab: PhysicsRuntime::GrabHandle) -> void error[RuntimeServicesError, PhysicsRuntime::PhysicsError]:
            raise RuntimeServicesError.SessionStopped if not session.application_live
            raise RuntimeServicesError.PhysicsUnavailable if not PhysicsRuntime::service_is_live(session.physics)
            try PhysicsRuntime::service_grab_end(session.physics, grab)
