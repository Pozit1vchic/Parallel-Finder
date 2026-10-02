#pragma once

#include "pfgpu/PoseEstimator.hpp"
#include <span>

namespace pfgpu::detail {

struct LetterboxTransform {
    float scale = 1.0F, padX = 0.0F, padY = 0.0F;
    int sourceWidth = 0, sourceHeight = 0;
};

// Serialized by the estimator. Keeps one bounded input buffer and one exact
// coordinate map, not decoded frames or a map for every source ever visited.
struct PoseInputWorkspace {
    const FloatTensor& build(std::span<const PoseImage> images, std::size_t batchSize,
                            const PoseEstimatorParams& params,
                            std::vector<LetterboxTransform>& transforms);
private:
    FloatTensor tensor_;
    std::vector<int> sourceX_, sourceY_;
    int imageWidth_ = 0, imageHeight_ = 0, inputWidth_ = 0, inputHeight_ = 0;
    int firstX_ = 0, lastX_ = 0;
    LetterboxTransform transform_;
    void mapImage(const PoseImage& image, const PoseEstimatorParams& params);
};

} // namespace pfgpu::detail
