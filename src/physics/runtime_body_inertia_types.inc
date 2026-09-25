        struct BodyInertia:
            private:
                principal_moments: Geometry::Vec3

        def BodyInertia(principal_moments: Geometry::Vec3) -> BodyInertia error[PhysicsError]:
            moments: Geometry::Vec3 = principal_moments
            raise PhysicsError.InvalidArgument if not Geometry::vec3_is_finite(moments) or moments.x < InertiaLimits::MIN_PRINCIPAL_MOMENT or moments.x > InertiaLimits::MAX_PRINCIPAL_MOMENT or moments.y < InertiaLimits::MIN_PRINCIPAL_MOMENT or moments.y > InertiaLimits::MAX_PRINCIPAL_MOMENT or moments.z < InertiaLimits::MIN_PRINCIPAL_MOMENT or moments.z > InertiaLimits::MAX_PRINCIPAL_MOMENT
            BodyInertia{principal_moments: moments}
