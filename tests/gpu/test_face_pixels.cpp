#include <gtest/gtest.h>
#include "../../gpu/src/FacePixels.hpp"
#include <bit>
#include <limits>
#include <vector>
#include <utility>

namespace {
// Literal pre-optimization implementation, deliberately separate from the
// shared-channel sampler. A one-bit input change can alter face identity.
float reference(const pfgpu::ReIdImage& image,double x,double y,int channel)
{
    if (!std::isfinite(x+y) || x<0 || y<0 || x>image.width-1 || y>image.height-1) return 0;
    const int ix=static_cast<int>(x),iy=static_cast<int>(y);
    const int nx=std::min(ix+1,image.width-1),ny=std::min(iy+1,image.height-1);
    const auto at=[&](int px,int py) { return image.rgba[(static_cast<std::size_t>(py)*image.width+px)*4+channel]; };
    const double dx=x-ix,dy=y-iy;
    return static_cast<float>((1-dy)*((1-dx)*at(ix,iy)+dx*at(nx,iy))
        +dy*((1-dx)*at(ix,ny)+dx*at(nx,ny)));
}
}
TEST(FacePixels, EveryChannelIsBitExactAcrossCropScalesAndRecognitionTransforms)
{
    for (const auto& dimensions : {std::pair{1,1},std::pair{7,3},std::pair{1937,1109}}) {
        const auto [width,height]=dimensions;
        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width)*height*4);
        for (std::size_t i=0;i<rgba.size();++i) rgba[i]=static_cast<std::uint8_t>((i*73+i/29)%256);
        pfgpu::ReIdImage image{width,height,rgba.data(),0,0,static_cast<float>(width),static_cast<float>(height)};
        const auto check=[&](double x,double y) {
            const auto rgb=pfgpu::detail::sampleFaceRgb(image,x,y);
            for(int c=0;c<3;++c)
                ASSERT_EQ(std::bit_cast<std::uint32_t>(rgb[c]),std::bit_cast<std::uint32_t>(reference(image,x,y,c)))
                    << width << 'x' << height << " at " << x << ',' << y << " channel " << c;
        };
        for (double scale : {0.125,0.32,1.0,2.5})
            for(int y=0;y<320;++y) for(int x=0;x<320;++x)
                check(3.25+(x+0.5)/scale-0.5,1.375+(y+0.5)/scale-0.5);
        for(int y=0;y<112;++y) for(int x=0;x<112;++x)
            check(1.125*(x-56.0252)-0.125*(y-71.7366)+width/2.0,
                  0.125*(x-56.0252)+1.125*(y-71.7366)+height/2.0);
        check(0,0); check(width-1,height-1); check(-0.01,0); check(0,height);
        check(std::numeric_limits<double>::quiet_NaN(),0);
        check(0,std::numeric_limits<double>::infinity());
    }
}
