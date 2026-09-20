#pragma once

#include "coordinate_abi.h"
#include "wiScene.h"

namespace probe {

// Submit local TRS and leave hierarchy propagation to the owning scene update.
inline bool submit_elisa_transform(const ElisaCoordinateProfile* profile,
    const ElisaTransformPayload* payload, wi::scene::TransformComponent* target) {
    ElisaTransformPayload converted{};
    if (target == nullptr || !elisa_transform_to_wicked(profile, payload, &converted)) return false;
    target->translation_local = XMFLOAT3(converted.position[0], converted.position[1], converted.position[2]);
    target->rotation_local = XMFLOAT4(converted.rotation_xyzw[0], converted.rotation_xyzw[1],
        converted.rotation_xyzw[2], converted.rotation_xyzw[3]);
    target->scale_local = XMFLOAT3(converted.scale[0], converted.scale[1], converted.scale[2]);
    target->SetDirty();
    return true;
}

inline bool submit_elisa_matrix(const ElisaCoordinateProfile* profile,
    const ElisaMatrixPayload* payload, wi::scene::TransformComponent* target) {
    if (!elisa_coordinate_profile_valid(profile) || !elisa_matrix_trs_payload_valid(payload) || target == nullptr) {
        return false;
    }
    ElisaMatrixPayload converted{};
    if (!elisa_matrix_to_wicked(payload, &converted)) return false;
    XMFLOAT4X4 matrix = {};
    for (size_t row = 0; row < 4; ++row) {
        for (size_t column = 0; column < 4; ++column) {
            matrix.m[row][column] = converted.values_column_major[row * 4 + column];
        }
    }
    matrix.m[3][0] *= profile->metres_per_unit;
    matrix.m[3][1] *= profile->metres_per_unit;
    matrix.m[3][2] *= profile->metres_per_unit;
    target->ClearTransform();
    target->MatrixTransform(matrix);
    return true;
}

inline bool read_elisa_transform(const ElisaCoordinateProfile* profile,
    const wi::scene::TransformComponent* source, ElisaTransformPayload* payload) {
    if (source == nullptr || payload == nullptr) return false;
    const XMFLOAT3& position = source->translation_local;
    const XMFLOAT4& rotation = source->rotation_local;
    const XMFLOAT3& scale = source->scale_local;
    const ElisaTransformPayload converted{{position.x, position.y, position.z},
        {rotation.x, rotation.y, rotation.z, rotation.w}, {scale.x, scale.y, scale.z}};
    return elisa_transform_from_wicked(profile, &converted, payload);
}

} // namespace probe
