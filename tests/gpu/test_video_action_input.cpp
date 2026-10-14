#include <gtest/gtest.h>
#include <pfgpu/VideoActionEstimator.hpp>
#include <limits>
#include <stdexcept>

TEST(VideoActionInput, PreservesRgbChannelsAndUniformTemporalOrder)
{
    std::array<std::vector<std::uint8_t>,5> pixels;
    std::vector<pfgpu::VideoActionFrame> frames;
    for(int time=0;time<5;++time) {
        auto& image=pixels[time];image.resize(5*5*4);
        for(int pixel=0;pixel<25;++pixel) {
            image[pixel*4]=time*50;image[pixel*4+1]=115;
            image[pixel*4+2]=255;image[pixel*4+3]=255;
        }
        frames.push_back({5,5,image.data()});
    }
    const std::array<pfgpu::VideoActionSubject,1> subjects{{{0,0,4,4}}};
    const auto input=pfgpu::prepareVideoActionInput(frames,subjects);
    EXPECT_EQ(input.clip.shape,(std::vector<std::int64_t>{1,3,32,256,256}));
    EXPECT_EQ(input.boxes.shape,(std::vector<std::int64_t>{1,5}));
    EXPECT_FLOAT_EQ(input.boxes.values[0],0);
    EXPECT_NEAR(input.boxes.values[3],204.8F,1e-4);
    for(int time=0;time<32;++time)for(int channel=0;channel<3;++channel) {
        const float value=channel==0 ? float((time*4/31)*50):channel==1 ? 115.F:255.F;
        const auto offset=(channel*32+time)*256*256;
        EXPECT_NEAR(input.clip.values[offset],(value/255.F-.45F)/.225F,1e-6);
        EXPECT_NEAR(input.clip.values[offset+128*256+128],input.clip.values[offset],1e-6);
    }
}

TEST(VideoActionInput, RejectsInvalidPeopleAndInconsistentClipBeforeModelLoading)
{
    std::vector<std::uint8_t> pixels(8*8*4,115);
    std::vector<pfgpu::VideoActionFrame> frames(5,{8,8,pixels.data()});
    const std::array<pfgpu::VideoActionSubject,1> valid{{{0,0,7,7}}};
    auto invalid=frames;invalid[2].rgba=nullptr;
    EXPECT_THROW(pfgpu::prepareVideoActionInput(invalid,valid),std::invalid_argument);
    invalid=frames;invalid[2].width=9;
    EXPECT_THROW(pfgpu::prepareVideoActionInput(invalid,valid),std::invalid_argument);
    EXPECT_THROW(pfgpu::prepareVideoActionInput(std::span(frames).first(4),valid),std::invalid_argument);
    for(const auto box:{pfgpu::VideoActionSubject{0,0,0,7},pfgpu::VideoActionSubject{9,0,10,7},
        pfgpu::VideoActionSubject{0,0,std::numeric_limits<float>::quiet_NaN(),7},
        pfgpu::VideoActionSubject{-3,0,0,7},pfgpu::VideoActionSubject{0,7,7,8}}) {
        const std::array<pfgpu::VideoActionSubject,1> subjects{{box}};
        EXPECT_THROW(pfgpu::VideoActionEstimator("absent-model.onnx").infer(frames,subjects),std::invalid_argument);
    }
}
