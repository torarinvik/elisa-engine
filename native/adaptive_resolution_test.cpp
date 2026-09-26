#include "adaptive_resolution.h"
#include <cassert>
#include <limits>
int main() {
    elisa::rendering::AdaptiveResolution policy;
    float scale = 1;
    for (int i=0;i<1000;++i) scale = policy.advance(0.04f, scale);
    assert(scale == 1); // opt-in
    policy.configure(60, 0.75f);
    assert(policy.advance(1.0f / 60, 0.5f) == 0.75f); // the configured floor applies on enable
    scale = 1;
    policy.configure(60, 0.75f);
    for (int i=0;i<60;++i) scale = policy.advance(0.04f, scale);
    assert(scale == 1); // startup hysteresis
    for (int i=0;i<2000;++i) scale = policy.advance(0.04f, scale);
    assert(scale == 0.75f); // bounded under sustained overload
    for (int i=0;i<2000;++i) scale = policy.advance(1.0f/60, scale);
    assert(scale == 0.75f); // budget noise does not oscillate
    for (int i=0;i<8000;++i) scale = policy.advance(0.008f, scale);
    assert(scale == 1); // recovery needs sustained headroom
    policy.configure(60, 0.75f);
    for (int i=0;i<100;++i) scale = policy.advance(0.5f, scale);
    assert(scale == 1); // asset loading is not GPU overload
    assert(policy.advance(std::numeric_limits<float>::quiet_NaN(), scale) == 1);
}
