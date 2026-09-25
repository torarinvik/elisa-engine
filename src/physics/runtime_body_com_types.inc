        const module CenterOfMassLimits:
            MAX_OFFSET: f32 = 1.0e4

        def center_of_mass_offset_valid(value: Geometry::Vec3) -> bool:
            return false if not Geometry::vec3_is_finite(value)
            return false if value.x < -CenterOfMassLimits::MAX_OFFSET or value.x > CenterOfMassLimits::MAX_OFFSET
            return false if value.y < -CenterOfMassLimits::MAX_OFFSET or value.y > CenterOfMassLimits::MAX_OFFSET
            return false if value.z < -CenterOfMassLimits::MAX_OFFSET or value.z > CenterOfMassLimits::MAX_OFFSET
            true
