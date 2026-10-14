#include <pfgpu/HeadPoseEstimator.hpp>
#include <gtest/gtest.h>
#include <limits>
#include <stdexcept>

TEST(HeadPoseInput, PreservesConstantRgbAndNormalizesChannelsIndependently) {
    for(const auto [width,height]:{std::pair{32,64},std::pair{257,256},std::pair{513,701}}) {
        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width)*height*4);
        for(std::size_t i=0;i<rgba.size();i+=4) {rgba[i]=127;rgba[i+1]=73;rgba[i+2]=241;rgba[i+3]=255;}
        const pfgpu::ReIdImage view{width,height,rgba.data(),0,0,static_cast<float>(width),static_cast<float>(height)};
        const auto input=pfgpu::prepareHeadPoseInput(view);
        EXPECT_EQ(input.shape,(std::vector<std::int64_t>{1,3,224,224}));ASSERT_EQ(input.values.size(),3*224*224U);
        const float expected[]={(127/255.F-.485F)/.229F,(73/255.F-.456F)/.224F,(241/255.F-.406F)/.225F};
        for(std::size_t i=0;i<input.values.size();++i)ASSERT_FLOAT_EQ(input.values[i],expected[i/(224*224)]);
    }
}
TEST(HeadPoseInput, RejectsInvalidFractionalOrUnboundedCrops) {
    std::vector<std::uint8_t> rgba(64*64*4);
    pfgpu::ReIdImage view{64,64,rgba.data(),0,0,64,64};
    for(const float right:{0.F,10.F,64.5F,std::numeric_limits<float>::quiet_NaN()}) {
        view.right=right;EXPECT_THROW(pfgpu::prepareHeadPoseInput(view),std::invalid_argument);
    }
    view.right=64;view.left=.5;EXPECT_THROW(pfgpu::prepareHeadPoseInput(view),std::invalid_argument);
    view.left=0;view.rgba=nullptr;EXPECT_THROW(pfgpu::prepareHeadPoseInput(view),std::invalid_argument);
}
