#pragma once

// One checked transform fixture shared by the render, physics, skin, and
// picking adapters. Keeping the fixture here prevents each vendor boundary
// from inventing its own handedness or scale conversion.
#include "coordinate_conventions.h"

namespace probe {
namespace coordinates {

struct CoordinateFixture {
    XMFLOAT3 position;
    XMFLOAT3 scale;
    XMFLOAT3 local_point;
    XMFLOAT3 ray_origin;
    XMFLOAT3 ray_direction;
};

inline CoordinateFixture asymmetric_fixture() {
    return CoordinateFixture{
        XMFLOAT3(4.25f, -1.5f, 7.75f),
        XMFLOAT3(-2.0f, 0.5f, 3.0f),
        XMFLOAT3(-0.75f, 2.25f, 1.125f),
        XMFLOAT3(-3.5f, 2.0f, 8.25f),
        XMFLOAT3(0.2f, -0.1f, -1.0f),
    };
}

inline XMFLOAT3 render_position(const CoordinateFixture& fixture) {
    return to_wicked(transform_point(fixture.local_point, fixture.position, fixture.scale));
}

inline XMFLOAT3 physics_position(const CoordinateFixture& fixture) {
    return to_wicked(fixture.position);
}

inline XMFLOAT3 skinned_position(const CoordinateFixture& fixture) {
    return render_position(fixture);
}

inline XMFLOAT3 picking_origin(const CoordinateFixture& fixture) {
    return to_wicked(fixture.ray_origin);
}

inline XMFLOAT3 picking_direction(const CoordinateFixture& fixture) {
    return to_wicked_direction(fixture.ray_direction);
}

} // namespace coordinates
} // namespace probe
