        struct BodyInertia:
            private:
                principal_moments: Geometry::Vec3
                principal_rotation: Geometry::Quat

        def BodyInertia(principal_moments: Geometry::Vec3,
            principal_rotation: Geometry::Quat) -> BodyInertia error[PhysicsError]:
            moments: Geometry::Vec3 = principal_moments
            rotation: Geometry::Quat = principal_rotation
            raise PhysicsError.InvalidArgument if not Geometry::vec3_is_finite(moments) or moments.x < InertiaLimits::MIN_PRINCIPAL_MOMENT or moments.x > InertiaLimits::MAX_PRINCIPAL_MOMENT or moments.y < InertiaLimits::MIN_PRINCIPAL_MOMENT or moments.y > InertiaLimits::MAX_PRINCIPAL_MOMENT or moments.z < InertiaLimits::MIN_PRINCIPAL_MOMENT or moments.z > InertiaLimits::MAX_PRINCIPAL_MOMENT or not Geometry::quat_is_finite(rotation) or Geometry::quat_length(rotation) <= 0.0
            BodyInertia{principal_moments: moments, principal_rotation: Geometry::quat_normalize(rotation)}

        def body_inertia_principal_moments(value: BodyInertia) -> Geometry::Vec3:
            value.principal_moments

        def body_inertia_principal_rotation(value: BodyInertia) -> Geometry::Quat:
            value.principal_rotation
