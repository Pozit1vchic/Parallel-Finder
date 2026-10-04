#pragma once

#include <pfgpu/ReIdEstimator.hpp>
#include <algorithm>
#include <array>
#include <cmath>

namespace pfgpu::detail {
// Share coordinate checks, four pixel addresses and interpolation geometry
// between RGB channels. Keep the original arithmetic order for exact inputs
// to both YuNet (BGR) and SFace (RGB), including zero-padding at image edges.
inline std::array<float,3> sampleFaceRgb(const ReIdImage& image, double x, double y)
{
    if (!std::isfinite(x+y) || x<0 || y<0 || x>image.width-1 || y>image.height-1) return {};
    const int ix=static_cast<int>(x), iy=static_cast<int>(y);
    const int nx=std::min(ix+1,image.width-1), ny=std::min(iy+1,image.height-1);
    const auto at = [&](int px,int py) {
        return image.rgba+(static_cast<std::size_t>(py)*image.width+px)*4;
    };
    const auto* a=at(ix,iy); const auto* b=at(nx,iy);
    const auto* c=at(ix,ny); const auto* d=at(nx,ny);
    const double dx=x-ix, dy=y-iy;
    std::array<float,3> rgb;
    for (std::size_t channel=0;channel<3;++channel)
        rgb[channel]=static_cast<float>((1-dy)*((1-dx)*a[channel]+dx*b[channel])
            +dy*((1-dx)*c[channel]+dx*d[channel]));
    return rgb;
}
}
