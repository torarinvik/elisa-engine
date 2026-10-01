        # R06: world bounds of a box, sphere, capsule or cylinder body's shape;
        # other shapes raise InvalidArgument.
        def body_bounds(world: World&, body: BodyHandle) -> Geometry::Bounds3 error[PhysicsError]:
            raise PhysicsError.ApplicationUnavailable if not world.live
            try native_body_bounds(world.generation, body)
