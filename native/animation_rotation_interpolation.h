#pragma once
#include <algorithm>
#include <cmath>

namespace elisa::animation {
// Shortest-arc SLERP keeps angular timing independent of the turn's size.
// Normalize inputs separately so imported quaternion magnitude cannot bias it.
inline void interpolate_rotation(const float* first, const float* second,
        float weight, float* output) {
    float a[4], b[4];
    float aa = 0, bb = 0;
    for (int i = 0; i < 4; ++i) { aa += first[i]*first[i]; bb += second[i]*second[i]; }
    const bool valid_a = std::isfinite(aa) && aa > 1e-12f;
    const bool valid_b = std::isfinite(bb) && bb > 1e-12f;
    for (int i = 0; i < 4; ++i) {
        a[i] = valid_a ? first[i]/std::sqrt(aa) : (i == 3 ? 1.f : 0.f);
        b[i] = valid_b ? second[i]/std::sqrt(bb) : a[i];
    }
    float dot = 0;
    for (int i = 0; i < 4; ++i) dot += a[i]*b[i];
    if (dot < 0) { for (float& v : b) v = -v; dot = -dot; }
    dot = std::clamp(dot, 0.f, 1.f);
    const float t = std::isfinite(weight) ? std::clamp(weight, 0.f, 1.f) : 0.f;
    float wa = 1-t, wb = t;
    if (dot < .9995f) {
        const float angle = std::acos(dot), denominator = std::sin(angle);
        wa = std::sin((1-t)*angle)/denominator;
        wb = std::sin(t*angle)/denominator;
    }
    float norm = 0;
    for (int i = 0; i < 4; ++i) { output[i] = wa*a[i]+wb*b[i]; norm += output[i]*output[i]; }
    for (int i = 0; i < 4; ++i) output[i] /= std::sqrt(norm);
}
} // namespace elisa::animation
