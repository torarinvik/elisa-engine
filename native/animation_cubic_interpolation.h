#pragma once
#include <algorithm>
#include <cmath>

namespace elisa::animation {
// Uniform-time PCHIP: shared tangents at interior keys, no scalar overshoot.
// Units are value per sample interval; clip sampling supplies neighbouring keys.
inline double uniform_shape_tangent(double before, double after) {
    if (before == 0 || after == 0 || std::signbit(before) != std::signbit(after)) return 0;
    return 2 * before * after / (before + after);
}
inline double uniform_shape_segment(double previous, double first, double second,
        double following, double t) {
    const double d = second-first;
    const double a = uniform_shape_tangent(first-previous, d);
    const double b = uniform_shape_tangent(d, following-second);
    const double t2=t*t, t3=t2*t;
    return (2*t3-3*t2+1)*first+(t3-2*t2+t)*a+(-2*t3+3*t2)*second+(t3-t2)*b;
}
// xyzw quaternion layout; translations3, quaternion4, scales3. All validation
// precedes publication and output may alias any input. Reject ambiguous near-
// 180-degree neighbours rather than guess an antipodal branch. This primitive
// supplies no loop wrapping, root-cycle unwrapping or playback policy itself.
inline bool interpolate_shape_transform(const float* previous, const float* first,
        const float* second, const float* following, float weight, float* output) {
    if (!previous || !first || !second || !following || !output ||
            !std::isfinite(weight) || weight<0 || weight>1) return false;
    const float* inputs[4]={previous,first,second,following};
    double keys[4][10];
    for (int k=0;k<4;++k) {
        for (int c=0;c<10;++c) {
            if (!std::isfinite(inputs[k][c])) return false;
            keys[k][c]=inputs[k][c];
        }
        double norm=0;
        for (int c=3;c<7;++c) norm+=keys[k][c]*keys[k][c];
        if (norm<=1e-12 || !std::isfinite(norm)) return false;
        for (int c=3;c<7;++c) keys[k][c]/=std::sqrt(norm);
        if (k) {
            double dot=0;for(int c=3;c<7;++c)dot+=keys[k-1][c]*keys[k][c];
            if (std::abs(dot)<1e-5) return false;
            if (dot<0) for(int c=3;c<7;++c)keys[k][c]=-keys[k][c];
        }
    }
    float result[10];double norm=0;
    for(int c=0;c<10;++c) {
        const double value=uniform_shape_segment(keys[0][c],keys[1][c],keys[2][c],keys[3][c],weight);
        if (!std::isfinite(value)) return false;
        result[c]=float(value);
        if (!std::isfinite(result[c])) return false;
        if(c>=3&&c<7)norm+=value*value;
    }
    if(norm<=1e-12 || !std::isfinite(norm))return false;
    for(int c=3;c<7;++c)result[c]=float(result[c]/std::sqrt(norm));
    std::copy(result,result+10,output);return true;
}
} // namespace elisa::animation
