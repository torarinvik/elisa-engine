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
    return transform->scale[0] != 0.0f && transform->scale[1] != 0.0f && transform->scale[2] != 0.0f;
}
