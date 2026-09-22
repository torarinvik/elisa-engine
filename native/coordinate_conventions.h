#pragma once

// Elisa owns a metre-sized, right-handed world with +Y up. Wicked is
// left-handed at this boundary, so the X axis is negated exactly once. Grid
// placement belongs here instead of being retyped by each native subsystem.
#include "wiScene.h"
#include "coordinate_abi.h"

#include <cmath>

namespace probe {
namespace coordinates {

constexpr float CELL_SPACING = 0.6f;
constexpr float GRID_ORIGIN = 2.1f;
constexpr float MARKER_HEIGHT = 1.0f;
constexpr float METRES_PER_UNIT = 1.0f;
constexpr float EPSILON = 0.0001f;

inline XMFLOAT3 to_wicked(float x, float y, float z) {
    return XMFLOAT3(-x, y, z);
}

inline XMFLOAT3 to_wicked(const XMFLOAT3& value) {
    return to_wicked(value.x, value.y, value.z);
}

inline XMFLOAT3 from_wicked(const XMFLOAT3& value) {
    return to_wicked(value);
}

inline XMFLOAT3 to_wicked_direction(const XMFLOAT3& value) {
    return to_wicked(value);
}

inline XMFLOAT3 from_wicked_direction(const XMFLOAT3& value) {
    return from_wicked(value);
}

// Conjugating a rotation by the reflection keeps its angle and turns its axis
// (x, y, z) into (x, -y, -z), so a quaternion keeps x and w. Scale is diagonal
// in the local frame and crosses unchanged (see elisa_transform_to_wicked).
inline XMFLOAT4 to_wicked_rotation(float x, float y, float z, float w) {
    return XMFLOAT4(x, -y, -z, w);
}

// Cooked tangents follow glTF, bitangent = cross(N, T) * W (see
// mesh_tangent_frames.h), and Wicked's shaders build cross(T, N) * W. The
// reflection turns cross(N, T) into cross(RT, RN), so the two sign changes
// cancel and W crosses unchanged. The coordinate ABI's tangent parity, -1 at
// the reflection, is for a bitangent built the same way on both sides.
inline XMFLOAT4 to_wicked_tangent(float x, float y, float z, float w) {
    return XMFLOAT4(-x, y, z, w);
}

// A glTF MAT4 is column-major and acts on column vectors. Copying its element
// order into XMFLOAT4X4 yields the equivalent DirectX row-vector matrix. Skin
// bind matrices live in mesh/joint space, so reflect both sides of the map.
inline XMFLOAT4X4 gltf_inverse_bind_to_wicked(const float* matrix) {
    const float reflection[4] = {-1.0f, 1.0f, 1.0f, 1.0f};
    XMFLOAT4X4 result{};
    for (size_t row = 0; row < 4; ++row) {
        for (size_t column = 0; column < 4; ++column) {
            result.m[row][column] = matrix[row * 4 + column] * reflection[row] * reflection[column];
        }
    }
    return result;
}

// Elisa pose matrices use DirectX-style row vectors. Change their basis with
// the same X reflection used by TRS submission and imported inverse binds.
inline XMFLOAT4X4 elisa_row_matrix_to_wicked(const XMFLOAT4X4& matrix) {
    const float reflection[4] = {-1.0f, 1.0f, 1.0f, 1.0f};
    XMFLOAT4X4 result{};
    for (size_t row = 0; row < 4; ++row) {
        for (size_t column = 0; column < 4; ++column) {
            result.m[row][column] = matrix.m[row][column] * reflection[row] * reflection[column];
        }
    }
    return result;
}

inline XMFLOAT3 scaled(const XMFLOAT3& value, const XMFLOAT3& scale) {
    return XMFLOAT3(value.x * scale.x, value.y * scale.y, value.z * scale.z);
}

inline XMFLOAT3 transform_point(const XMFLOAT3& point, const XMFLOAT3& translation,
    const XMFLOAT3& scale) {
    const XMFLOAT3 local = scaled(point, scale);
    return XMFLOAT3(local.x + translation.x, local.y + translation.y, local.z + translation.z);
}

inline bool near_equal(const XMFLOAT3& left, const XMFLOAT3& right, float epsilon = EPSILON) {
    return std::abs(left.x - right.x) <= epsilon && std::abs(left.y - right.y) <= epsilon &&
        std::abs(left.z - right.z) <= epsilon;
}

inline bool finite(const XMFLOAT3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

// The single RH->LH reflection flips triangle orientation. A negative scale
// flips it again when its determinant is negative, so the backend can choose
// culling from one shared rule instead of per-mesh sign hacks.
inline bool winding_reversed(const XMFLOAT3& scale) {
    const float components[3] = {scale.x, scale.y, scale.z};
    const ElisaCoordinateProfile profile = elisa_coordinate_profile();
    int32_t parity = 0;
    return elisa_transform_tangent_parity(&profile, components, 1, &parity) && parity < 0;
}

inline XMFLOAT3 cell_to_wicked(int cell_x, int cell_y, float height = MARKER_HEIGHT) {
    return to_wicked(
        static_cast<float>(cell_x) * CELL_SPACING - GRID_ORIGIN,
        static_cast<float>(cell_y) * CELL_SPACING - GRID_ORIGIN,
        height);
}

} // namespace coordinates
} // namespace probe
