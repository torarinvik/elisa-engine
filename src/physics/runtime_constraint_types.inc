        enum ConstraintKind:
            Fixed
            Point
            Distance
            Hinge
            Slider
            Cone
            SixDOF
            SwingTwist

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
            cone_half_angle: f32
            translation_min: Geometry::Vec3
            translation_max: Geometry::Vec3
            rotation_min: Geometry::Vec3
            rotation_max: Geometry::Vec3
            swing_normal_half_cone_angle: f32
            swing_plane_half_cone_angle: f32
            swing_min_twist_angle: f32
            swing_max_twist_angle: f32

        def ConstraintDesc(kind: ConstraintKind, body_a: BodyHandle,
            body_b: BodyHandle, anchor: Geometry::Vec3,
            frame_rotation: Geometry::Quat) -> ConstraintDesc:
            ConstraintDesc{kind: kind, body_a: body_a, body_b: body_b,
                anchor: anchor, frame_rotation: frame_rotation,
                lower_limit: 0.0, upper_limit: 0.0,
                target_velocity: 0.0, max_motor_force: 0.0,
                break_distance: 0.0, disable_collision: false,
                cone_half_angle: 0.0,
                translation_min: Geometry::vec3_zero(),
                translation_max: Geometry::vec3_zero(),
                rotation_min: Geometry::vec3_zero(),
                rotation_max: Geometry::vec3_zero(),
                swing_normal_half_cone_angle: 0.0,
                swing_plane_half_cone_angle: 0.0,
                swing_min_twist_angle: 0.0, swing_max_twist_angle: 0.0}

        struct ConstraintHandle:
            private:
                world_generation: u64
                slot: u32
                generation: u64

        def ConstraintHandle() -> ConstraintHandle:
            ConstraintHandle{world_generation: 0, slot: 0, generation: 0}
