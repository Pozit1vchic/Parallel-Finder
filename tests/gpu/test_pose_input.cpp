#include <gtest/gtest.h>
#include <pfgpu/detail/PoseInput.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace {
pfgpu::FloatTensor referenceInput(std::span<const pfgpu::PoseImage> images, std::size_t batch,
                                  const pfgpu::PoseEstimatorParams& params)
{
    const auto plane = static_cast<std::size_t>(params.inputWidth) * params.inputHeight;
    pfgpu::FloatTensor output;
    output.shape = {static_cast<std::int64_t>(batch), 3, params.inputHeight, params.inputWidth};
    output.values.assign(batch * 3U * plane, 114.0F / 255.0F);
    for (std::size_t b = 0; b < batch; ++b) {
        const auto& image = images[std::min(b, images.size() - 1)];
        const float scale = std::min(static_cast<float>(params.inputWidth) / image.width,
                                     static_cast<float>(params.inputHeight) / image.height);
        const float padX = (params.inputWidth - std::max(1, static_cast<int>(std::lround(image.width * scale)))) * .5F;
        const float padY = (params.inputHeight - std::max(1, static_cast<int>(std::lround(image.height * scale)))) * .5F;
        for (int y = 0; y < params.inputHeight; ++y)
            for (int x = 0; x < params.inputWidth; ++x) {
                const float sx = (static_cast<float>(x) - padX) / scale;
                const float sy = (static_cast<float>(y) - padY) / scale;
                if (sx < 0 || sx >= image.width || sy < 0 || sy >= image.height) continue;
                const auto* pixel = image.rgba + (static_cast<std::size_t>(std::clamp(static_cast<int>(sy), 0, image.height - 1))
                    * image.width + std::clamp(static_cast<int>(sx), 0, image.width - 1)) * 4U;
                const auto offset = b * 3U * plane + static_cast<std::size_t>(y) * params.inputWidth + x;
                for (std::size_t channel = 0; channel < 3; ++channel)
                    output.values[offset + channel * plane] = pixel[channel] / 255.0F;
            }
    }
    return output;
}

std::vector<std::uint8_t> pixels(int width, int height, int seed)
{
    std::vector<std::uint8_t> data(static_cast<std::size_t>(width) * height * 4U);
    for (std::size_t i = 0; i < data.size(); ++i)
        data[i] = static_cast<std::uint8_t>((i * 37 + i / 11 + seed) % 256);
    return data;
}
}

TEST(PoseInput, EveryFloatMatchesLegacyReferenceWithoutEnlargement)
{
    pfgpu::detail::PoseInputWorkspace workspace;
    std::vector<pfgpu::detail::LetterboxTransform> transforms;
    pfgpu::PoseEstimatorParams params;
    for (const auto& [width, height] : {std::pair{1280, 720}, {1920, 960}, {320, 641}, {641, 320},
                                      {1, 1}, {3, 17}, {17, 3}, {640, 640}}) {
        for (const auto& [targetWidth, targetHeight] : {std::pair{640, 640}, {319, 513}}) {
            params.inputWidth = targetWidth; params.inputHeight = targetHeight;
            if(std::min(static_cast<float>(targetWidth)/width,static_cast<float>(targetHeight)/height)>1)continue;
            for (const int seed : {0, 131}) {
                const auto rgba = pixels(width, height, seed);
                const pfgpu::PoseImage image{width, height, rgba.data()};
                const std::array images{image};
                const auto reference = referenceInput(images, 1, params);
                const auto& actual = workspace.build(images, 1, params, transforms);
                EXPECT_EQ(actual.shape, reference.shape);
                ASSERT_EQ(actual.values.size(), reference.values.size());
                EXPECT_EQ(std::memcmp(actual.values.data(), reference.values.data(), actual.values.size() * sizeof(float)), 0)
                    << width << 'x' << height << " -> " << targetWidth << 'x' << targetHeight;
                ASSERT_EQ(transforms.size(), 1U);
                EXPECT_EQ(transforms[0].sourceWidth, width);
                EXPECT_EQ(transforms[0].sourceHeight, height);
            }
        }
    }
}

TEST(PoseInput, EnlargementInterpolatesPixelCentersAndKeepsLetterboxPadding)
{
    pfgpu::detail::PoseInputWorkspace workspace;
    std::vector<pfgpu::detail::LetterboxTransform> transforms;
    pfgpu::PoseEstimatorParams params;params.inputWidth=4;params.inputHeight=4;
    const std::array<std::uint8_t,8> rgba{0,64,255,0,255,128,0,255};
    const std::array images{pfgpu::PoseImage{2,1,rgba.data()}};
    const auto& actual=workspace.build(images,1,params,transforms);
    const std::array<std::array<unsigned,4>,3> expected{{{0,64,191,255},{64,80,112,128},{255,191,64,0}}};
    for(std::size_t channel=0;channel<3;++channel)
        for(std::size_t y=0;y<4;++y)for(std::size_t x=0;x<4;++x)
            EXPECT_FLOAT_EQ(actual.values[channel*16+y*4+x],
                (y==0||y==3?114.f:static_cast<float>(expected[channel][x]))/255.f);
    EXPECT_FLOAT_EQ(transforms.front().padY,1.f);
}

TEST(PoseInput, OnlyActuallyEnlargedSourcesChangePreprocessingContract)
{
    EXPECT_TRUE(pfgpu::poseInputEnlarges(160,120));
    EXPECT_TRUE(pfgpu::poseInputEnlarges(320,240));
    EXPECT_FALSE(pfgpu::poseInputEnlarges(640,100));
    EXPECT_FALSE(pfgpu::poseInputEnlarges(100,640));
    EXPECT_FALSE(pfgpu::poseInputEnlarges(1280,720));
    EXPECT_FALSE(pfgpu::poseInputEnlarges(1920,1080));
    EXPECT_FALSE(pfgpu::poseInputEnlarges(0,120));
    EXPECT_FALSE(pfgpu::poseInputEnlarges(512,512,512,512));
}

TEST(PoseInput, EnlargedOddPaddingAndSinglePixelTailAreBounded)
{
    pfgpu::detail::PoseInputWorkspace workspace;
    std::vector<pfgpu::detail::LetterboxTransform> transforms;
    pfgpu::PoseEstimatorParams params;params.inputWidth=5;params.inputHeight=5;
    const auto rgba=pixels(2,1,0);
    const std::array images{pfgpu::PoseImage{2,1,rgba.data()}};
    const auto& actual=workspace.build(images,2,params,transforms);
    // Python/OpenCV's 2.5 -> 2 ties-to-even resize leaves one top and two
    // bottom pad rows. The fixed-batch tail duplicates the exact first input.
    EXPECT_FLOAT_EQ(transforms.front().padY,1.f);
    for(std::size_t c=0;c<3;++c)for(std::size_t x=0;x<5;++x) {
        EXPECT_FLOAT_EQ(actual.values[c*25+x],114.f/255.f);
        EXPECT_FLOAT_EQ(actual.values[c*25+15+x],114.f/255.f);
        EXPECT_FLOAT_EQ(actual.values[c*25+20+x],114.f/255.f);
    }
    EXPECT_TRUE(std::equal(actual.values.begin(),actual.values.begin()+75,actual.values.begin()+75));
    const std::array<std::uint8_t,4> pixel{23,47,81,255};
    const std::array one{pfgpu::PoseImage{1,1,pixel.data()}};
    const auto& constant=workspace.build(one,1,params,transforms);
    for(std::size_t c=0;c<3;++c)for(std::size_t i=0;i<25;++i)
        EXPECT_FLOAT_EQ(constant.values[c*25+i],pixel[c]/255.f);
}

TEST(PoseInput, EnlargementInterpolatesBothAxesWithReplicatedEdges)
{
    pfgpu::detail::PoseInputWorkspace workspace;
    std::vector<pfgpu::detail::LetterboxTransform> transforms;
    pfgpu::PoseEstimatorParams params;params.inputWidth=4;params.inputHeight=4;
    const std::array<std::uint8_t,16> rgba{0,0,0,255,64,64,64,255,128,128,128,255,255,255,255,255};
    const std::array images{pfgpu::PoseImage{2,2,rgba.data()}};
    const std::array<unsigned,16> expected{0,16,48,64,32,52,92,112,96,124,179,207,128,160,223,255};
    const auto& actual=workspace.build(images,1,params,transforms);
    for(std::size_t channel=0;channel<3;++channel)for(std::size_t i=0;i<16;++i)
        EXPECT_FLOAT_EQ(actual.values[channel*16+i],expected[i]/255.f);
}

TEST(PoseInput, MixedBatchAndTailAreExactAndDoNotRetainPreviousPixels)
{
    pfgpu::detail::PoseInputWorkspace workspace;
    std::vector<pfgpu::detail::LetterboxTransform> transforms;
    pfgpu::PoseEstimatorParams params;
    const auto wide = pixels(641, 320, 7), tall = pixels(320, 641, 97);
    const std::array images{pfgpu::PoseImage{641, 320, wide.data()}, pfgpu::PoseImage{320, 641, tall.data()}};
    const auto reference = referenceInput(images, 8, params);
    const auto& actual = workspace.build(images, 8, params, transforms);
    EXPECT_EQ(actual.values, reference.values);
    ASSERT_EQ(transforms.size(), 8U);
    for (std::size_t i = 1; i < transforms.size(); ++i) {
        EXPECT_EQ(transforms[i].sourceWidth, 320);
        EXPECT_FLOAT_EQ(transforms[i].padX, transforms[1].padX);
        EXPECT_FLOAT_EQ(transforms[i].padY, transforms[1].padY);
    }
    const auto* allocation = actual.values.data();
    const std::array single{images[0]};
    const auto& resized = workspace.build(single, 1, params, transforms);
    EXPECT_EQ(resized.values.data(), allocation);
    EXPECT_EQ(resized.values, referenceInput(single, 1, params).values);
    EXPECT_EQ(transforms.size(), 1U);
}

TEST(PoseInput, RejectsBadShapesAndOversizedBatchesBeforeAllocation)
{
    pfgpu::detail::PoseInputWorkspace workspace;
    std::vector<pfgpu::detail::LetterboxTransform> transforms;
    pfgpu::PoseEstimatorParams params;
    const auto rgba = pixels(8, 8, 0);
    const std::array images{pfgpu::PoseImage{8, 8, rgba.data()}};
    EXPECT_THROW(workspace.build({}, 1, params, transforms), std::invalid_argument);
    EXPECT_THROW(workspace.build(images, 0, params, transforms), std::invalid_argument);
    EXPECT_THROW(workspace.build(images, std::numeric_limits<std::size_t>::max(), params, transforms), std::invalid_argument);
    params.inputWidth = 0;
    EXPECT_THROW(workspace.build(images, 1, params, transforms), std::invalid_argument);
    params.inputWidth = std::numeric_limits<int>::max();
    params.inputHeight = std::numeric_limits<int>::max();
    EXPECT_THROW(workspace.build(images, 1, params, transforms), std::invalid_argument);
    params = {};
    const std::array invalid{pfgpu::PoseImage{8, 8, nullptr}};
    EXPECT_THROW(workspace.build(invalid, 1, params, transforms), std::invalid_argument);
    EXPECT_NO_THROW(workspace.build(images, 1, params, transforms));
}
