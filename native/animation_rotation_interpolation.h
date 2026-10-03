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
// Carry a captured local correction along the moving outgoing FK transform.
// Translation offsets and scale ratios stay local; rotation delta is applied
// in parent space. Output may alias corrected. No heap allocation is required.
inline bool rebase_local_correction(const float* corrected, const float* reference,
        const float* moving, float* output) {
    if (!corrected || !reference || !moving || !output) return false;
    float result[10];
    for (int i = 0; i < 10; ++i)
        if (!std::isfinite(corrected[i]) || !std::isfinite(reference[i]) || !std::isfinite(moving[i])) return false;
    for (const float* transform : {corrected, reference, moving}) {
        float norm = 0.f;
        for (int i = 3; i < 7; ++i) norm += transform[i]*transform[i];
        if (!std::isfinite(norm) || norm <= 1e-12f) return false;
    }
    for (int i = 0; i < 3; ++i) {
        if (reference[i+7] == 0.f) return false;
        result[i] = corrected[i] + moving[i] - reference[i];
        result[i+7] = corrected[i+7] * moving[i+7] / reference[i+7];
    }
    const auto multiply = [](const float* a, const float* b, float* out) {
        out[0] = a[3]*b[0]+a[0]*b[3]+a[1]*b[2]-a[2]*b[1];
        out[1] = a[3]*b[1]-a[0]*b[2]+a[1]*b[3]+a[2]*b[0];
        out[2] = a[3]*b[2]+a[0]*b[1]-a[1]*b[0]+a[2]*b[3];
        out[3] = a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2];
    };
    float inverse[4], c[4], next[4], delta[4], rotation[4];
    interpolate_rotation(reference+3, reference+3, 0.f, inverse);
    inverse[0] = -inverse[0]; inverse[1] = -inverse[1]; inverse[2] = -inverse[2];
    interpolate_rotation(corrected+3, corrected+3, 0.f, c);
    interpolate_rotation(moving+3, moving+3, 0.f, next);
    multiply(c, inverse, delta);
    multiply(delta, next, rotation);
    interpolate_rotation(rotation, rotation, 0.f, result+3);
    for (float value : result) if (!std::isfinite(value)) return false;
    std::copy(result, result+10, output);
    return true;
}
} // namespace elisa::animation
