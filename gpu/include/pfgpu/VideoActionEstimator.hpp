#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>
#include "pfgpu/Inference.hpp"

namespace pfgpu {

struct VideoActionFrame {
    int width=0,height=0;
    const std::uint8_t* rgba=nullptr;
};
struct VideoActionSubject {
    float left=0,top=0,right=0,bottom=0;
};
struct VideoActionInput {
    FloatTensor clip;
    FloatTensor boxes;
};

// AVA's RGB 32-frame, short-side-256, mean .45/std .225 input contract.
// Subject boxes refer to the clip midpoint, not actor identity. The caller
// must supply a continuous, selected-person interval and owned RGBA pixels.
VideoActionInput prepareVideoActionInput(std::span<const VideoActionFrame> frames,
                                        std::span<const VideoActionSubject> subjects);

class VideoActionEstimator {
public:
    VideoActionEstimator(std::string modelPath,Provider provider=Provider::Auto,
                         std::size_t intraOpThreads=1);
    // Raw 80-label AVA scores, not calibrated correctness probabilities.
    std::vector<std::array<double,80>> infer(std::span<const VideoActionFrame> frames,
                                          std::span<const VideoActionSubject> subjects);
private:
    std::string modelPath_;
    Provider provider_;
    std::size_t intraOpThreads_;
};
}
