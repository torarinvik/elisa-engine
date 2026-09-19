#pragma once

#include "coordinate_conventions.h"
#include "probe_core.h"

#include <cstdio>

namespace probe {

inline bool probe_coordinate_conventions() {
    const XMFLOAT3 point(1.25f, -2.5f, 3.75f);
    const XMFLOAT3 wicked = coordinates::to_wicked(point);
    const XMFLOAT3 round_trip = coordinates::from_wicked(wicked);
    if (!check(coordinates::near_equal(point, round_trip), "coordinate point round trip") ||
        !check(coordinates::near_equal(coordinates::to_wicked_direction(point), wicked),
            "coordinate direction reflection") || !check(coordinates::finite(wicked),
            "coordinate finite point")) {
        return false;
    }
    const XMFLOAT3 translation(4.25f, -1.5f, 7.75f);
    const XMFLOAT3 scale(-2.0f, 0.5f, 3.0f);
    const XMFLOAT3 local(-0.75f, 2.25f, 1.125f);
    const XMFLOAT3 transformed = coordinates::transform_point(local, translation, scale);
    const XMFLOAT3 backend_transformed = coordinates::to_wicked(transformed);
    const XMFLOAT3 reconstructed = coordinates::from_wicked(backend_transformed);
    if (!check(coordinates::near_equal(transformed, reconstructed),
        "negative nonuniform transform round trip") ||
        !check(coordinates::winding_reversed(XMFLOAT3(2.0f, 3.0f, 4.0f)),
            "positive scale includes handedness reflection") ||
        !check(!coordinates::winding_reversed(scale),
            "negative scale restores winding parity")) {
        return false;
    }
    const XMFLOAT3 camera_origin(-3.5f, 2.0f, 8.25f);
    const XMFLOAT3 camera_forward(0.2f, -0.1f, -1.0f);
    const XMFLOAT3 ray_origin = coordinates::to_wicked(camera_origin);
    const XMFLOAT3 ray_direction = coordinates::to_wicked_direction(camera_forward);
    if (!check(coordinates::near_equal(camera_origin, coordinates::from_wicked(ray_origin)),
        "picking ray origin round trip") ||
        !check(coordinates::near_equal(camera_forward,
            coordinates::from_wicked_direction(ray_direction)),
            "picking ray direction round trip")) {
        return false;
    }
    std::fprintf(stdout, "coordinates: point=(%.2f,%.2f,%.2f) scale_parity=%d ray=(%.2f,%.2f,%.2f)\n",
        point.x, point.y, point.z, coordinates::winding_reversed(scale) ? -1 : 1,
        ray_direction.x, ray_direction.y, ray_direction.z);
    return true;
}

} // namespace probe
