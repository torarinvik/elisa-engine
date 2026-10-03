#include "../native/animation_cubic_interpolation.h"
#include <cassert>
#include <limits>
#include <cstring>
#include <iostream>
using namespace elisa::animation;
int main() {
    float keys[5][10]{};
    for(int i=0;i<5;++i){keys[i][0]=float(i*i);keys[i][5]=std::sin(.08f*i);keys[i][6]=std::cos(.08f*i);keys[i][7]=keys[i][8]=keys[i][9]=1;}
    float out[10];
    for(float t:{0.f,1.f}) {assert(interpolate_shape_transform(keys[0],keys[1],keys[2],keys[3],t,out));assert(std::abs(out[0]-(t==0?1:4))<1e-6);assert(std::abs(out[6]-(t==0?keys[1][6]:keys[2][6]))<1e-6);}
    for(int i=0;i<=1000;++i){assert(interpolate_shape_transform(keys[0],keys[1],keys[2],keys[3],i/1000.f,out));assert(out[0]>=1 && out[0]<=4);double norm=0;for(int c=3;c<7;++c)norm+=out[c]*out[c];assert(std::abs(norm-1)<2e-7);}
    const float e=1e-3f;float left[10],mid[10],right[10];
    assert(interpolate_shape_transform(keys[0],keys[1],keys[2],keys[3],1-e,left));assert(interpolate_shape_transform(keys[0],keys[1],keys[2],keys[3],1,mid));assert(interpolate_shape_transform(keys[1],keys[2],keys[3],keys[4],e,right));
    for(int c:{0,5,6})assert(std::abs((mid[c]-left[c])/e-(right[c]-mid[c])/e)<.015f);
    float baseline[10];assert(interpolate_shape_transform(keys[0],keys[1],keys[2],keys[3],.37f,baseline));
    for(int k=0;k<4;++k)for(int c=3;c<7;++c)keys[k][c]*= k%2 ? -5.f : .02f;
    assert(interpolate_shape_transform(keys[0],keys[1],keys[2],keys[3],.37f,out));for(int c=0;c<10;++c)assert(std::abs(out[c]-baseline[c])<2e-7);
    assert(interpolate_shape_transform(keys[0],keys[1],keys[2],keys[3],.37f,keys[1]));for(int c=0;c<10;++c)assert(std::abs(keys[1][c]-baseline[c])<2e-7);
    std::memcpy(out,baseline,sizeof out);keys[2][0]=std::numeric_limits<float>::quiet_NaN();assert(!interpolate_shape_transform(keys[0],keys[1],keys[2],keys[3],.5f,out));assert(std::memcmp(out,baseline,sizeof out)==0);
    keys[2][0]=4;for(int c=3;c<7;++c)keys[2][c]=0;assert(!interpolate_shape_transform(keys[0],keys[1],keys[2],keys[3],.5f,out));assert(std::memcmp(out,baseline,sizeof out)==0);
    assert(!interpolate_shape_transform(nullptr,keys[1],keys[2],keys[3],.5f,out));assert(!interpolate_shape_transform(keys[0],keys[1],keys[2],keys[3],-1,out));
    float ambiguous[4][10]{};for(auto& k:ambiguous)k[6]=1;ambiguous[2][6]=0;ambiguous[2][3]=1;
    assert(!interpolate_shape_transform(ambiguous[0],ambiguous[1],ambiguous[2],ambiguous[3],.5f,out));assert(std::memcmp(out,baseline,sizeof out)==0);
    assert(uniform_shape_tangent(1,-1)==0);assert(uniform_shape_tangent(0,1)==0);
    std::cout<<"PASS cubic keys, bounds, unit quaternions, derivatives, scaled antipodes, aliasing and invalid-input atomicity\n";
}
