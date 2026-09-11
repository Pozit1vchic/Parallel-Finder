#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "pfgpu/Inference.hpp"

namespace pfgpu {

struct PoseImage {
    int width = 0;
    int height = 0;
    const std::uint8_t* rgba = nullptr;
};

struct PoseDetection {
    float confidence = 0.0F;
    float left = 0.0F;
    float top = 0.0F;
    float right = 0.0F;
    float bottom = 0.0F;
    std::vector<float> keypoints; // x, y, confidence triplets in source pixels
};

struct PoseEstimatorParams {
    int inputWidth = 640;
    int inputHeight = 640;
    std::size_t keypointCount = 17;
    float confidenceThreshold = 0.25F;
    Provider provider = Provider::Auto;
    std::string profile = "b1";
};

class PoseEstimator {
public:
    PoseEstimator(std::string modelPath, PoseEstimatorParams params = {});

    std::vector<PoseDetection> infer(const PoseImage& image);
    const std::string& modelPath() const noexcept { return modelPath_; }
    const PoseEstimatorParams& params() const noexcept { return params_; }

private:
    std::string modelPath_;
    PoseEstimatorParams params_;
    SessionCache sessions_;
};

} // namespace pfgpu
