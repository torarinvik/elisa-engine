#pragma once

#include <cstddef>
#include <cstdint>

namespace elisa::assets {

inline constexpr uint32_t MAX_GEOMETRY_SKIN_BONES = 256;
inline constexpr uint32_t MAX_GEOMETRY_RIG_NODES = 256;
inline constexpr uint32_t MAX_GEOMETRY_ANIMATION_CLIPS = 32;
// Total animation floats per package (joints x frames x 10); two full boxing
// stance libraries at 120 Hz need more than the original 2M.
inline constexpr size_t MAX_GEOMETRY_ANIMATION_SAMPLE_FLOATS = 4'000'000;

} // namespace elisa::assets
