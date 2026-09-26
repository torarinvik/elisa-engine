#pragma once
#include <algorithm>
#include <cmath>

namespace elisa::rendering {
inline constexpr float ADAPTIVE_RESOLUTION_DEFAULT_MINIMUM = 0.75f;
inline constexpr float ADAPTIVE_RESOLUTION_MAX_SAMPLE_DT = 0.1f;
inline constexpr float ADAPTIVE_RESOLUTION_MEASURE_WINDOW = 2.0f;
inline constexpr unsigned ADAPTIVE_RESOLUTION_MIN_SAMPLES = 30;
inline constexpr float ADAPTIVE_RESOLUTION_STEP = 0.0625f;
inline constexpr float ADAPTIVE_RESOLUTION_OVERLOAD_FACTOR = 1.10f;
inline constexpr float ADAPTIVE_RESOLUTION_HEADROOM_FACTOR = 0.80f;
inline constexpr float ADAPTIVE_RESOLUTION_COOLDOWN = 3.0f;
inline constexpr float ADAPTIVE_RESOLUTION_RECOVERY_COOLDOWN = 6.0f;

// Frame-cadence controller, not a GPU timer. A conservative floor protects
// readability when the bottleneck is CPU work that resolution cannot fix.
struct AdaptiveResolution {
    float target = 0;
    float minimum = ADAPTIVE_RESOLUTION_DEFAULT_MINIMUM;
    float elapsed = 0;
    float total = 0;
    unsigned samples = 0;
    float cooldown = ADAPTIVE_RESOLUTION_COOLDOWN;
    void configure(float fps, float floor) {
        target = fps;
        minimum = floor;
        elapsed = total = 0;
        samples = 0;
        cooldown = ADAPTIVE_RESOLUTION_COOLDOWN;
    }
    float advance(float dt, float current) {
        if (target <= 0 || !std::isfinite(dt) || dt <= 0) return current;
        current = std::clamp(current, minimum, 1.0f);
        // Ignore load stalls and let temporal history settle after resizing.
        if (dt > ADAPTIVE_RESOLUTION_MAX_SAMPLE_DT) {
            elapsed = total = 0;
            samples = 0;
            cooldown = ADAPTIVE_RESOLUTION_COOLDOWN;
            return current;
        }
        if (cooldown > 0) { cooldown -= dt; return current; }
        elapsed += dt; total += dt; ++samples;
        if (elapsed < ADAPTIVE_RESOLUTION_MEASURE_WINDOW ||
            samples < ADAPTIVE_RESOLUTION_MIN_SAMPLES) return current;
        const float mean = total / samples;
        elapsed = total = 0; samples = 0;
        const float budget = 1 / target;
        float result = current;
        if (mean > budget * ADAPTIVE_RESOLUTION_OVERLOAD_FACTOR)
            result = std::max(minimum, current - ADAPTIVE_RESOLUTION_STEP);
        else if (mean < budget * ADAPTIVE_RESOLUTION_HEADROOM_FACTOR)
            result = std::min(1.0f, current + ADAPTIVE_RESOLUTION_STEP);
        if (result != current)
            cooldown = result > current ? ADAPTIVE_RESOLUTION_RECOVERY_COOLDOWN : ADAPTIVE_RESOLUTION_COOLDOWN;
        return result;
    }
};
}
