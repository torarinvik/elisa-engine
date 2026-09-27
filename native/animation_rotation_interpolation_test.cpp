#include "animation_rotation_interpolation.h"
#include <cassert>
#include <limits>
int main() {
    float a[4]={0,0,0,2}, b[4]={0,0,3,0}, out[4];
    // 180 degree turn must cover 45 degrees at quarter time even with unequal norms.
    elisa::animation::interpolate_rotation(a,b,.25f,out);
    assert(std::abs(out[2]-std::sin(3.14159265f/8))<1e-6f);
    assert(std::abs(out[3]-std::cos(3.14159265f/8))<1e-6f);
    // Several points on a 120 degree arc must retain uniform angular progress.
    float turn[4]={0,std::sin(3.14159265f/3),0,std::cos(3.14159265f/3)};
    for (int step=0; step<=20; ++step) {
        float t=step/20.f;
        elisa::animation::interpolate_rotation(a,turn,t,out);
        assert(std::abs(out[1]-std::sin(t*3.14159265f/3))<1e-6f);
        assert(std::abs(out[3]-std::cos(t*3.14159265f/3))<1e-6f);
    }
    float sign[4]={0,0,0,-5};
    elisa::animation::interpolate_rotation(a,sign,.4f,out);
    assert(std::abs(out[3]-1)<1e-6f);
    float bad[4]={std::numeric_limits<float>::quiet_NaN(),0,0,0};
    elisa::animation::interpolate_rotation(a,bad,.5f,out);
    assert(std::abs(out[3]-1)<1e-6f);
    float close[4]={0,0,.0001f,1};
    elisa::animation::interpolate_rotation(a,close,.5f,out);
    assert(std::isfinite(out[2]) && std::abs(out[2]-.00005f)<1e-7f);
    elisa::animation::interpolate_rotation(a,b,1,out);
    assert(std::abs(out[2]-1)<1e-6f && std::abs(out[3])<1e-6f);
}
