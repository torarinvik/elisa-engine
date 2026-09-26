        enum ConstraintKind:
            Fixed
            Point
            Distance
            Hinge
            Slider

        # Limits are distances for Distance/Slider, angles in radians for Hinge.
        # A zero break_distance disables distance-based breaking. Motor force means
        # torque for Hinge and force for Slider; zero velocity disables the motor.
        struct ConstraintDesc:
            kind: ConstraintKind
            body_a: BodyHandle
            body_b: BodyHandle
            anchor: Geometry::Vec3
            frame_rotation: Geometry::Quat
            lower_limit: f32
            upper_limit: f32
            target_velocity: f32
            max_motor_force: f32
            break_distance: f32
            disable_collision: bool

        def ConstraintDesc(kind: ConstraintKind, body_a: BodyHandle,
            body_b: BodyHandle, anchor: Geometry::Vec3,
            frame_rotation: Geometry::Quat) -> ConstraintDesc:
            ConstraintDesc{kind: kind, body_a: body_a, body_b: body_b,
                anchor: anchor, frame_rotation: frame_rotation,
                lower_limit: 0.0, upper_limit: 0.0,
                target_velocity: 0.0, max_motor_force: 0.0,
                break_distance: 0.0, disable_collision: false}

        struct ConstraintHandle:
            private:
                world_generation: u64
                slot: u32
                generation: u64
