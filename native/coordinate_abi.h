#pragma once

// Vendor-free transform metadata shared by Elisa-facing adapters. The native
// renderer may transpose or reflect internally, but it must validate this
// profile before consuming a transform payload.
#include <cmath>
#include <cstddef>
#include <cstdint>

enum {
    ELISA_COORDINATE_ABI_VERSION = 1u,
    ELISA_COORDINATE_COLUMN_MAJOR = 0u,
    ELISA_COORDINATE_RIGHT_HANDED = 0u,
    ELISA_COORDINATE_DEPTH_ZERO_TO_ONE = 1u,
    ELISA_COORDINATE_QUATERNION_XYZW = 0u,
};

typedef struct ElisaCoordinateProfile {
    uint32_t abi_version;
    uint32_t matrix_order;
    uint32_t handedness;
    uint32_t depth_range;
    uint32_t quaternion_layout;
    uint32_t tangent_parity;
    float metres_per_unit;
    uint32_t reserved;
} ElisaCoordinateProfile;

typedef struct ElisaTransformPayload {
    float position[3];
    float rotation_xyzw[4];
    float scale[3];
} ElisaTransformPayload;

typedef struct ElisaMatrixPayload {
    float values_column_major[16];
} ElisaMatrixPayload;

static inline ElisaCoordinateProfile elisa_coordinate_profile() {
    return ElisaCoordinateProfile{
        ELISA_COORDINATE_ABI_VERSION,
        ELISA_COORDINATE_COLUMN_MAJOR,
        ELISA_COORDINATE_RIGHT_HANDED,
        ELISA_COORDINATE_DEPTH_ZERO_TO_ONE,
        ELISA_COORDINATE_QUATERNION_XYZW,
        1u,
        1.0f,
        0u,
    };
}

static inline bool elisa_coordinate_profile_valid(const ElisaCoordinateProfile* profile) {
    return profile != nullptr && profile->abi_version == ELISA_COORDINATE_ABI_VERSION &&
        profile->matrix_order == ELISA_COORDINATE_COLUMN_MAJOR &&
        profile->handedness == ELISA_COORDINATE_RIGHT_HANDED &&
        profile->depth_range == ELISA_COORDINATE_DEPTH_ZERO_TO_ONE &&
        profile->quaternion_layout == ELISA_COORDINATE_QUATERNION_XYZW &&
        (profile->tangent_parity == 1u || profile->tangent_parity == 0u) &&
        std::isfinite(profile->metres_per_unit) && profile->metres_per_unit > 0.0f &&
        profile->reserved == 0u;
}

static inline bool elisa_transform_payload_valid(const ElisaTransformPayload* transform) {
    if (transform == nullptr) return false;
    for (size_t index = 0; index < 3; ++index) {
        if (!std::isfinite(transform->position[index]) || !std::isfinite(transform->scale[index])) {
            return false;
        }
    }
    for (size_t index = 0; index < 4; ++index) {
        if (!std::isfinite(transform->rotation_xyzw[index])) return false;
    }
    const float x = transform->rotation_xyzw[0];
    const float y = transform->rotation_xyzw[1];
    const float z = transform->rotation_xyzw[2];
    const float w = transform->rotation_xyzw[3];
    const float rotation_length_squared = x * x + y * y + z * z + w * w;
    return transform->scale[0] != 0.0f && transform->scale[1] != 0.0f && transform->scale[2] != 0.0f &&
        std::isfinite(rotation_length_squared) && rotation_length_squared > 1e-12f;
}

static inline bool elisa_transform_to_wicked(const ElisaCoordinateProfile* profile,
    const ElisaTransformPayload* source, ElisaTransformPayload* target) {
    if (!elisa_coordinate_profile_valid(profile) || !elisa_transform_payload_valid(source) || target == nullptr) {
        return false;
    }
    ElisaTransformPayload converted{};
    for (size_t index = 0; index < 3; ++index) {
        converted.position[index] = source->position[index] * profile->metres_per_unit;
        converted.scale[index] = source->scale[index];
    }
    const float x = source->rotation_xyzw[0];
    const float y = source->rotation_xyzw[1];
    const float z = source->rotation_xyzw[2];
    const float w = source->rotation_xyzw[3];
    const float inverse_length = 1.0f / std::sqrt(x * x + y * y + z * z + w * w);
    converted.position[0] = -converted.position[0];
    converted.rotation_xyzw[0] = x * inverse_length;
    converted.rotation_xyzw[1] = -y * inverse_length;
    converted.rotation_xyzw[2] = -z * inverse_length;
    converted.rotation_xyzw[3] = w * inverse_length;
    if (!std::isfinite(converted.position[0]) || !std::isfinite(converted.position[1]) ||
        !std::isfinite(converted.position[2])) return false;
    *target = converted;
    return true;
}

static inline bool elisa_transform_from_wicked(const ElisaCoordinateProfile* profile,
    const ElisaTransformPayload* source, ElisaTransformPayload* target) {
    if (!elisa_coordinate_profile_valid(profile) || !elisa_transform_payload_valid(source) || target == nullptr) {
        return false;
    }
    ElisaTransformPayload converted{};
    const float inverse_scale = 1.0f / profile->metres_per_unit;
    converted.position[0] = -source->position[0] * inverse_scale;
    converted.position[1] = source->position[1] * inverse_scale;
    converted.position[2] = source->position[2] * inverse_scale;
    converted.scale[0] = source->scale[0];
    converted.scale[1] = source->scale[1];
    converted.scale[2] = source->scale[2];
    const float x = source->rotation_xyzw[0];
    const float y = source->rotation_xyzw[1];
    const float z = source->rotation_xyzw[2];
    const float w = source->rotation_xyzw[3];
    const float inverse_length = 1.0f / std::sqrt(x * x + y * y + z * z + w * w);
    converted.rotation_xyzw[0] = x * inverse_length;
    converted.rotation_xyzw[1] = -y * inverse_length;
    converted.rotation_xyzw[2] = -z * inverse_length;
    converted.rotation_xyzw[3] = w * inverse_length;
    if (!elisa_transform_payload_valid(&converted)) return false;
    *target = converted;
    return true;
}

static inline bool elisa_matrix_payload_valid(const ElisaMatrixPayload* matrix) {
    if (matrix == nullptr) return false;
    for (size_t index = 0; index < 16; ++index) {
        if (!std::isfinite(matrix->values_column_major[index])) return false;
    }
    return std::abs(matrix->values_column_major[3]) <= 1e-6f &&
        std::abs(matrix->values_column_major[7]) <= 1e-6f &&
        std::abs(matrix->values_column_major[11]) <= 1e-6f &&
        std::abs(matrix->values_column_major[15] - 1.0f) <= 1e-6f;
}

// Wicked's TransformComponent stores TRS, so matrix submission rejects shear
// and zero scale instead of silently losing information during decomposition.
static inline bool elisa_matrix_trs_payload_valid(const ElisaMatrixPayload* matrix) {
    if (!elisa_matrix_payload_valid(matrix)) return false;
    float length_squared[3] = {};
    for (size_t column = 0; column < 3; ++column) {
        for (size_t row = 0; row < 3; ++row) {
            const float value = matrix->values_column_major[column * 4 + row];
            length_squared[column] += value * value;
        }
        if (!std::isfinite(length_squared[column]) || length_squared[column] <= 1e-12f) return false;
    }
    for (size_t first = 0; first < 3; ++first) {
        for (size_t second = first + 1; second < 3; ++second) {
            float dot = 0.0f;
            for (size_t row = 0; row < 3; ++row) {
                dot += matrix->values_column_major[first * 4 + row] *
                    matrix->values_column_major[second * 4 + row];
            }
            const float tolerance = 1e-4f * std::sqrt(length_squared[first]) * std::sqrt(length_squared[second]);
            if (!std::isfinite(dot) || std::abs(dot) > tolerance) return false;
        }
    }
    return true;
}

// Change basis by reflecting X on both sides: M_wicked = S * M_elisa * S.
// The payload remains column-major; row and column signs are applied by index.
static inline bool elisa_matrix_to_wicked(const ElisaMatrixPayload* source, ElisaMatrixPayload* target) {
    if (!elisa_matrix_payload_valid(source) || target == nullptr) return false;
    const float reflection[4] = {-1.0f, 1.0f, 1.0f, 1.0f};
    for (size_t column = 0; column < 4; ++column) {
        for (size_t row = 0; row < 4; ++row) {
            const size_t index = column * 4 + row;
            target->values_column_major[index] = source->values_column_major[index] *
                reflection[row] * reflection[column];
        }
    }
    return true;
}

static inline bool elisa_matrix_from_wicked(const ElisaMatrixPayload* source, ElisaMatrixPayload* target) {
    return elisa_matrix_to_wicked(source, target);
}
