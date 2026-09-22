#pragma once

#include "coordinate_conventions.h"
#include "coordinate_fixture.h"
#include "coordinate_abi.h"
#include "coordinate_transform_bridge.h"
#include "probe_core.h"

#include <cstdio>

namespace probe {

inline bool probe_coordinate_conventions(wi::scene::Scene& scene) {
    const ElisaCoordinateProfile profile = elisa_coordinate_profile();
    if (!check(elisa_coordinate_profile_valid(&profile), "coordinate ABI profile") ||
        !check(profile.tangent_parity == -1, "coordinate ABI reflects tangent parity") ||
        !check(sizeof(ElisaCoordinateProfile) == 32 && sizeof(ElisaTransformPayload) == 40 &&
            sizeof(ElisaMatrixPayload) == 64,
            "coordinate ABI layout")) {
        return false;
    }
    ElisaCoordinateProfile invalid_profile = profile;
    invalid_profile.depth_range = 2;
    if (!check(!elisa_coordinate_profile_valid(&invalid_profile), "coordinate ABI rejects depth mismatch")) {
        return false;
    }
    invalid_profile = profile;
    invalid_profile.tangent_parity = 0;
    if (!check(!elisa_coordinate_profile_valid(&invalid_profile), "coordinate ABI rejects ambiguous tangent parity")) {
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
    const float tangent_scale_positive[3] = {2.0f, 3.0f, 4.0f};
    const float tangent_scale_reflected[3] = {-2.0f, 3.0f, 4.0f};
    const float tangent_scale_double_reflected[3] = {-2.0f, -3.0f, 4.0f};
    int32_t tangent_parity = 0;
    if (!check(elisa_transform_tangent_parity(&profile, tangent_scale_positive, 1, &tangent_parity) &&
            tangent_parity == -1, "positive scale includes basis tangent reflection") ||
        !check(elisa_transform_tangent_parity(&profile, tangent_scale_reflected, 1, &tangent_parity) &&
            tangent_parity == 1, "negative scale restores tangent parity") ||
        !check(elisa_transform_tangent_parity(&profile, tangent_scale_double_reflected, 1, &tangent_parity) &&
            tangent_parity == -1, "two negative axes preserve one tangent reflection")) {
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
    ElisaTransformPayload invalid_rotation = payload;
    invalid_rotation.rotation_xyzw[0] = 0.0f;
    invalid_rotation.rotation_xyzw[1] = 0.0f;
    invalid_rotation.rotation_xyzw[2] = 0.0f;
    invalid_rotation.rotation_xyzw[3] = 0.0f;
    if (!check(!elisa_transform_payload_valid(&invalid_rotation), "coordinate ABI rejects zero quaternion")) {
        return false;
    }
    const ElisaMatrixPayload matrix{{
        2.0f, 0.5f, -0.2f, 0.0f,
        0.3f, 3.0f, 0.7f, 0.0f,
        -0.1f, 0.4f, 1.5f, 0.0f,
        4.25f, -1.5f, 7.75f, 1.0f,
    }};
    ElisaMatrixPayload wicked_matrix{};
    ElisaMatrixPayload matrix_round_trip{};
    if (!check(elisa_matrix_to_wicked(&matrix, &wicked_matrix) &&
            elisa_matrix_from_wicked(&wicked_matrix, &matrix_round_trip),
            "column-major matrix basis conversion")) return false;
    ElisaMatrixPayload invalid_matrix = matrix;
    invalid_matrix.values_column_major[3] = 0.5f;
    if (!check(!elisa_matrix_payload_valid(&invalid_matrix), "coordinate ABI rejects projective matrix")) return false;
    if (!check(elisa_matrix_payload_valid(&matrix) && !elisa_matrix_trs_payload_valid(&matrix),
            "coordinate ABI distinguishes affine shear from TRS")) return false;
    const float local_point[4] = {-0.75f, 2.25f, 1.125f, 1.0f};
    const float wicked_point[4] = {0.75f, 2.25f, 1.125f, 1.0f};
    float source_result[4] = {};
    float backend_result[4] = {};
    for (size_t row = 0; row < 4; ++row) {
        for (size_t column = 0; column < 4; ++column) {
            source_result[row] += matrix.values_column_major[column * 4 + row] * local_point[column];
            backend_result[row] += wicked_matrix.values_column_major[column * 4 + row] * wicked_point[column];
        }
    }
    if (!check(std::abs(source_result[0] + backend_result[0]) < 0.0001f &&
            std::abs(source_result[1] - backend_result[1]) < 0.0001f &&
            std::abs(source_result[2] - backend_result[2]) < 0.0001f &&
            std::abs(source_result[3] - backend_result[3]) < 0.0001f,
            "matrix conversion preserves asymmetric point transform")) return false;
    const float inverse_bind[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        -2.0f, -1.0f, 0.0f, 1.0f,
    };
    const XMFLOAT4X4 wicked_inverse_bind = coordinates::gltf_inverse_bind_to_wicked(inverse_bind);
    if (!check(std::abs(wicked_inverse_bind.m[3][0] - 2.0f) < 0.0001f &&
            std::abs(wicked_inverse_bind.m[3][1] + 1.0f) < 0.0001f &&
            wicked_inverse_bind.m[0][0] == 1.0f && wicked_inverse_bind.m[3][3] == 1.0f,
            "glTF inverse-bind matrix converts to Wicked row space")) return false;
    XMFLOAT4X4 authored_pose{};
    authored_pose.m[0][0] = 1.0f;
    authored_pose.m[1][1] = 1.0f;
    authored_pose.m[2][2] = 1.0f;
    authored_pose.m[3][0] = 2.0f;
    authored_pose.m[3][1] = -1.0f;
    authored_pose.m[3][3] = 1.0f;
    const XMFLOAT4X4 wicked_pose = coordinates::elisa_row_matrix_to_wicked(authored_pose);
    if (!check(std::abs(wicked_pose.m[3][0] + 2.0f) < 0.0001f &&
            std::abs(wicked_pose.m[3][1] + 1.0f) < 0.0001f &&
            wicked_pose.m[0][0] == 1.0f && wicked_pose.m[3][3] == 1.0f,
            "animation pose matrix reflects into Wicked row space")) return false;
    for (size_t index = 0; index < 16; ++index) {
        if (!check(std::abs(matrix_round_trip.values_column_major[index] - matrix.values_column_major[index]) < 0.0001f,
                "matrix basis round trip")) return false;
    }
    ElisaCoordinateProfile scaled_profile = profile;
    scaled_profile.metres_per_unit = 0.01f;
    const ElisaTransformPayload authored{{fixture.position.x, fixture.position.y, fixture.position.z},
        {0.2f, -0.4f, 0.1f, 0.85f}, {fixture.scale.x, fixture.scale.y, fixture.scale.z}};
    const float quaternion_length = std::sqrt(0.2f * 0.2f + 0.4f * 0.4f + 0.1f * 0.1f + 0.85f * 0.85f);
    const float normalized_authored_rotation[4] = {0.2f / quaternion_length, -0.4f / quaternion_length,
        0.1f / quaternion_length, 0.85f / quaternion_length};
    ElisaTransformPayload converted_authored{};
    if (!check(elisa_transform_to_wicked(&scaled_profile, &authored, &converted_authored),
            "quaternion basis conversion")) return false;
    auto quaternion_matrix = [](const float q[4], float result[3][3]) {
        const float x = q[0], y = q[1], z = q[2], w = q[3];
        result[0][0] = 1.0f - 2.0f * (y * y + z * z);
        result[0][1] = 2.0f * (x * y - z * w);
        result[0][2] = 2.0f * (x * z + y * w);
        result[1][0] = 2.0f * (x * y + z * w);
        result[1][1] = 1.0f - 2.0f * (x * x + z * z);
        result[1][2] = 2.0f * (y * z - x * w);
        result[2][0] = 2.0f * (x * z - y * w);
        result[2][1] = 2.0f * (y * z + x * w);
        result[2][2] = 1.0f - 2.0f * (x * x + y * y);
    };
    float source_rotation[3][3] = {};
    float wicked_rotation[3][3] = {};
    quaternion_matrix(normalized_authored_rotation, source_rotation);
    quaternion_matrix(converted_authored.rotation_xyzw, wicked_rotation);
    const float reflection[3] = {-1.0f, 1.0f, 1.0f};
    for (size_t row = 0; row < 3; ++row) {
        for (size_t column = 0; column < 3; ++column) {
            if (!check(std::abs(wicked_rotation[row][column] -
                    source_rotation[row][column] * reflection[row] * reflection[column]) < 0.0001f,
                    "quaternion matches matrix basis conversion")) return false;
        }
    }
    const wi::ecs::Entity transform_entity = scene.Entity_CreateCube("elisa_coordinate_transform");
    auto* submitted_transform = scene.transforms.GetComponent(transform_entity);
    if (!check(submitted_transform != nullptr && submit_elisa_transform(&scaled_profile, &authored,
            submitted_transform), "Wicked TRS transform submission")) return false;
    ElisaTransformPayload reconstructed_payload{};
    if (!check(read_elisa_transform(&scaled_profile, submitted_transform, &reconstructed_payload) &&
            std::abs(reconstructed_payload.position[0] - authored.position[0]) < 0.0001f &&
            std::abs(reconstructed_payload.position[1] - authored.position[1]) < 0.0001f &&
            std::abs(reconstructed_payload.position[2] - authored.position[2]) < 0.0001f &&
            std::abs(reconstructed_payload.scale[0] - authored.scale[0]) < 0.0001f &&
            std::abs(reconstructed_payload.scale[1] - authored.scale[1]) < 0.0001f &&
            std::abs(reconstructed_payload.scale[2] - authored.scale[2]) < 0.0001f &&
            std::abs(reconstructed_payload.rotation_xyzw[0] - 0.2f / quaternion_length) < 0.0001f &&
            std::abs(reconstructed_payload.rotation_xyzw[1] + 0.4f / quaternion_length) < 0.0001f &&
            std::abs(reconstructed_payload.rotation_xyzw[2] - 0.1f / quaternion_length) < 0.0001f &&
            std::abs(reconstructed_payload.rotation_xyzw[3] - 0.85f / quaternion_length) < 0.0001f,
            "Wicked TRS transform round trip")) return false;
    const ElisaMatrixPayload authored_matrix{{
        2.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 3.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 4.0f, 0.0f,
        4.25f, -1.5f, 7.75f, 1.0f,
    }};
    if (!check(submit_elisa_matrix(&scaled_profile, &authored_matrix, submitted_transform) &&
            std::abs(submitted_transform->translation_local.x + 0.0425f) < 0.0001f &&
            std::abs(submitted_transform->translation_local.y + 0.015f) < 0.0001f &&
            std::abs(submitted_transform->translation_local.z - 0.0775f) < 0.0001f &&
            std::abs(submitted_transform->scale_local.x - 2.0f) < 0.0001f &&
            std::abs(submitted_transform->scale_local.y - 3.0f) < 0.0001f &&
            std::abs(submitted_transform->scale_local.z - 4.0f) < 0.0001f,
            "Wicked column-major matrix submission")) return false;
    const float matrix_translation_before_rejection = submitted_transform->translation_local.x;
    if (!check(!submit_elisa_matrix(&scaled_profile, &matrix, submitted_transform) &&
            std::abs(submitted_transform->translation_local.x - matrix_translation_before_rejection) < 0.0001f,
            "Wicked matrix bridge rejects shear atomically")) return false;
    scene.Entity_Remove(transform_entity);
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
