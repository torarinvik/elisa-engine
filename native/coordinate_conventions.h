#pragma once

// Elisa owns a metre-sized, right-handed world with +Y up. Wicked is
// left-handed at this boundary, so the X axis is negated exactly once. Grid
// placement belongs here instead of being retyped by each native subsystem.
#include "wiScene.h"

namespace probe {
namespace coordinates {

constexpr float CELL_SPACING = 0.6f;
constexpr float GRID_ORIGIN = 2.1f;
constexpr float MARKER_HEIGHT = 1.0f;

inline XMFLOAT3 to_wicked(float x, float y, float z) {
    return XMFLOAT3(-x, y, z);
}

inline XMFLOAT3 cell_to_wicked(int cell_x, int cell_y, float height = MARKER_HEIGHT) {
    return to_wicked(
        static_cast<float>(cell_x) * CELL_SPACING - GRID_ORIGIN,
        static_cast<float>(cell_y) * CELL_SPACING - GRID_ORIGIN,
        height);
}

} // namespace coordinates
} // namespace probe
