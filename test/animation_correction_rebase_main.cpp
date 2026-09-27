#include "../native/animation_rotation_interpolation.h"
#include <cassert>
#include <limits>

int main() {
    float reference[10] = {1,2,3, 0,0,0,1, 1,2,-2};
    float corrected[10] = {1.5f,2,3, 0,0,.70710678f,.70710678f, 1,3,-3};
    float moving[10] = {4,5,6, .70710678f,0,0,.70710678f, 2,4,-4};
    float result[10];
    assert(elisa::animation::rebase_local_correction(corrected, reference, moving, result));
    assert(std::abs(result[0]-4.5f) < 1e-6f && result[1] == 5 && result[2] == 6);
    for (int i=3;i<7;++i) assert(std::abs(result[i]-.5f)<1e-6f);
    assert(result[7] == 2 && result[8] == 6 && result[9] == -6);
    assert(elisa::animation::rebase_local_correction(corrected, reference, reference, result));
    for (int i=0;i<10;++i) assert(std::abs(result[i]-corrected[i])<1e-6f);
    assert(elisa::animation::rebase_local_correction(reference, reference, moving, result));
    for (int i=0;i<10;++i) assert(std::abs(result[i]-moving[i])<1e-6f);
    assert(elisa::animation::rebase_local_correction(corrected, reference, moving, corrected));
    assert(std::abs(corrected[0]-4.5f)<1e-6f);
    for (float& v:result) v=123;
    reference[7]=0;
    assert(!elisa::animation::rebase_local_correction(corrected, reference, moving, result));
    for (float v:result) assert(v==123);
    reference[7]=1;
    moving[3]=std::numeric_limits<float>::quiet_NaN();
    assert(!elisa::animation::rebase_local_correction(corrected, reference, moving, result));
    for (float v:result) assert(v==123);
    moving[3]=moving[4]=moving[5]=moving[6]=0;
    assert(!elisa::animation::rebase_local_correction(corrected, reference, moving, result));
    assert(!elisa::animation::rebase_local_correction(nullptr, reference, moving, result));
}
