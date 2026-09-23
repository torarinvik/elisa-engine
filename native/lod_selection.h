#pragma once

// Deterministic screen-error selection shared by the runtime and native tests.
#include "lod_manifest.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>

namespace elisa::assets {

inline constexpr double LOD_SELECTION_HYSTERESIS = 0.1;

inline double lod_error_pixels(const LodManifest& manifest, size_t level,
        double world_scale, double projection_pixels_per_world_unit) {
    if (!manifest.valid || level >= manifest.level_count || !std::isfinite(world_scale) ||
        world_scale <= 0.0 || !std::isfinite(projection_pixels_per_world_unit) ||
        projection_pixels_per_world_unit <= 0.0) return std::numeric_limits<double>::infinity();
    const double result = manifest.levels[level].relative_error_budget * manifest.object_extent *
        world_scale * projection_pixels_per_world_unit;
    return std::isfinite(result) ? result : std::numeric_limits<double>::infinity();
}

inline size_t select_lod_level(const LodManifest& manifest, double world_scale,
        double projection_pixels_per_world_unit, double pixel_budget, size_t current_level) {
    if (!manifest.valid || manifest.level_count == 0 || manifest.level_count > MAX_LOD_LEVELS ||
        current_level >= manifest.level_count || !std::isfinite(pixel_budget) || pixel_budget <= 0.0) {
        return MAX_LOD_LEVELS;
    }
    const double current_error = lod_error_pixels(manifest, current_level, world_scale,
        projection_pixels_per_world_unit);
    if (!std::isfinite(current_error)) return MAX_LOD_LEVELS;
    size_t selected = 0;
    for (size_t index = 1; index < manifest.level_count; ++index) {
        if (lod_error_pixels(manifest, index, world_scale,
                projection_pixels_per_world_unit) > pixel_budget) break;
        selected = index;
    }
    if (selected > current_level) {
        const double candidate_error = lod_error_pixels(manifest, selected, world_scale,
            projection_pixels_per_world_unit);
        return candidate_error <= pixel_budget * (1.0 - LOD_SELECTION_HYSTERESIS)
            ? selected : current_level;
    }
    if (selected < current_level &&
        current_error <= pixel_budget * (1.0 + LOD_SELECTION_HYSTERESIS)) return current_level;
    return selected;
}

} // namespace elisa::assets
