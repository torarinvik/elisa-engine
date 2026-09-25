#pragma once

// Converts physics query values at the Elisa/Wicked boundary. Positions and
// physical lengths use the coordinate profile's unit scale; directions only
// change basis, while velocities and impulses also use the unit scale.
#include "coordinate_conventions.h"

namespace elisa_physics_coordinates {

inline bool to_wicked_position(const ElisaCoordinateProfile* profile,
    const XMFLOAT3& source, XMFLOAT3& target) {
    if (!elisa_coordinate_profile_valid(profile) || !probe::coordinates::finite(source)) return false;
    const XMFLOAT3 reflected = probe::coordinates::to_wicked(source);
    target = XMFLOAT3(reflected.x * profile->metres_per_unit,
        reflected.y * profile->metres_per_unit, reflected.z * profile->metres_per_unit);
    return probe::coordinates::finite(target);
}

inline bool from_wicked_position(const ElisaCoordinateProfile* profile,
    const XMFLOAT3& source, XMFLOAT3& target) {
    if (!elisa_coordinate_profile_valid(profile) || !probe::coordinates::finite(source)) return false;
    const XMFLOAT3 reflected = probe::coordinates::from_wicked(source);
    const float units_per_metre = 1.0f / profile->metres_per_unit;
    target = XMFLOAT3(reflected.x * units_per_metre,
        reflected.y * units_per_metre, reflected.z * units_per_metre);
    return probe::coordinates::finite(target);
}

inline bool to_wicked_direction(const ElisaCoordinateProfile* profile,
    const XMFLOAT3& source, XMFLOAT3& target) {
    if (!elisa_coordinate_profile_valid(profile) || !probe::coordinates::finite(source)) return false;
    target = probe::coordinates::to_wicked_direction(source);
    return probe::coordinates::finite(target);
}

inline bool from_wicked_direction(const ElisaCoordinateProfile* profile,
    const XMFLOAT3& source, XMFLOAT3& target) {
    if (!elisa_coordinate_profile_valid(profile) || !probe::coordinates::finite(source)) return false;
    target = probe::coordinates::from_wicked_direction(source);
    return probe::coordinates::finite(target);
}

inline bool to_wicked_physical_vector(const ElisaCoordinateProfile* profile,
    const XMFLOAT3& source, XMFLOAT3& target) {
    if (!elisa_coordinate_profile_valid(profile) || !probe::coordinates::finite(source)) return false;
    const XMFLOAT3 reflected = probe::coordinates::to_wicked_direction(source);
    target = XMFLOAT3(reflected.x * profile->metres_per_unit,
        reflected.y * profile->metres_per_unit, reflected.z * profile->metres_per_unit);
    return probe::coordinates::finite(target);
}

inline bool from_wicked_physical_vector(const ElisaCoordinateProfile* profile,
    const XMFLOAT3& source, XMFLOAT3& target) {
    if (!elisa_coordinate_profile_valid(profile) || !probe::coordinates::finite(source)) return false;
    const XMFLOAT3 reflected = probe::coordinates::from_wicked_direction(source);
    const float units_per_metre = 1.0f / profile->metres_per_unit;
    target = XMFLOAT3(reflected.x * units_per_metre,
        reflected.y * units_per_metre, reflected.z * units_per_metre);
    return probe::coordinates::finite(target);
}

inline bool to_wicked_length(const ElisaCoordinateProfile* profile,
    float source, float& target) {
    if (!elisa_coordinate_profile_valid(profile) || !std::isfinite(source) || source < 0.0f) return false;
    target = source * profile->metres_per_unit;
    return std::isfinite(target);
}

inline bool from_wicked_length(const ElisaCoordinateProfile* profile,
    float source, float& target) {
    if (!elisa_coordinate_profile_valid(profile) || !std::isfinite(source) || source < 0.0f) return false;
    target = source / profile->metres_per_unit;
    return std::isfinite(target);
}

} // namespace elisa_physics_coordinates
