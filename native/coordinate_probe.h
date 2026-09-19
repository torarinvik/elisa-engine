#pragma once

#include "coordinate_conventions.h"
#include "coordinate_fixture.h"
#include "coordinate_abi.h"
#include "probe_core.h"

#include <cstdio>

namespace probe {

inline bool probe_coordinate_conventions() {
    const ElisaCoordinateProfile profile = elisa_coordinate_profile();
    if (!check(elisa_coordinate_profile_valid(&profile), "coordinate ABI profile") ||
        !check(sizeof(ElisaCoordinateProfile) == 32 && sizeof(ElisaTransformPayload) == 40,
            "coordinate ABI layout")) {
        return false;
    }
    ElisaCoordinateProfile invalid_profile = profile;
    invalid_profile.depth_range = 2;
    if (!check(!elisa_coordinate_profile_valid(&invalid_profile), "coordinate ABI rejects depth mismatch")) {
        return false;
    }
    const XMFLOAT3 point(1.25f, -2.5f, 3.75f);
    const XMFLOAT3 wicked = coordinates::to_wicked(point);
    const XMFLOAT3 round_trip = coordinates::from_wicked(wicked);
    if (!check(coordinates::near_equal(point, round_trip), "coordinate point round trip") ||
        !check(coordinates::near_equal(coordinates::to_wicked_direction(point), wicked),
            "coordinate direction reflection") || !check(coordinates::finite(wicked),
            "coordinate finite point")) {
        return false;
    }
    const auto fixture = coordinates::asymmetric_fixture();
    const XMFLOAT3 translation = fixture.position;
    const XMFLOAT3 scale = fixture.scale;
    const XMFLOAT3 local = fixture.local_point;
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
    const ElisaTransformPayload payload{{transformed.x, transformed.y, transformed.z},
        {0.0f, 0.0f, 0.0f, 1.0f}, {scale.x, scale.y, scale.z}};
    if (!check(elisa_transform_payload_valid(&payload), "coordinate transform payload") ||
        !check(!elisa_transform_payload_valid(nullptr), "coordinate ABI rejects null transform")) {
        return false;
    }
    const XMFLOAT3 camera_origin = fixture.ray_origin;
    const XMFLOAT3 camera_forward = fixture.ray_direction;
    const XMFLOAT3 ray_origin = coordinates::to_wicked(camera_origin);
    const XMFLOAT3 ray_direction = coordinates::to_wicked_direction(camera_forward);
    if (!check(coordinates::near_equal(camera_origin, coordinates::from_wicked(ray_origin)),
        "picking ray origin round trip") ||
        !check(coordinates::near_equal(camera_forward,
            coordinates::from_wicked_direction(ray_direction)),
            "picking ray direction round trip")) {
        return false;
    }
    if (!check(coordinates::near_equal(coordinates::render_position(fixture), backend_transformed),
            "render fixture uses shared conversion") ||
        !check(coordinates::near_equal(coordinates::skinned_position(fixture), backend_transformed),
            "skin fixture uses shared conversion") ||
        !check(coordinates::near_equal(coordinates::physics_position(fixture),
            coordinates::to_wicked(translation)), "physics fixture uses shared conversion") ||
        !check(coordinates::near_equal(coordinates::picking_origin(fixture), ray_origin),
            "picking fixture uses shared conversion") ||
        !check(coordinates::near_equal(coordinates::picking_direction(fixture), ray_direction),
            "picking direction uses shared conversion")) {
        return false;
    }
    std::fprintf(stdout, "coordinates: point=(%.2f,%.2f,%.2f) scale_parity=%d ray=(%.2f,%.2f,%.2f)\n",
        point.x, point.y, point.z, coordinates::winding_reversed(scale) ? -1 : 1,
        ray_direction.x, ray_direction.y, ray_direction.z);
    return true;
}

} // namespace probe
