#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <mutex>

#include "pfgpu/Inference.hpp"

namespace pfgpu {
namespace detail { struct PoseInputWorkspace; }

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

enum class PoseOutputMode {
    Auto,
    EndToEnd,
    ExternalNms,
};

struct PoseEstimatorParams {
    int inputWidth = 640;
    int inputHeight = 640;
    std::size_t keypointCount = 17;
    float confidenceThreshold = 0.25F;
    float nmsIouThreshold = 0.70F;
    Provider provider = Provider::Auto;
    std::string profile = "b1";
    std::size_t batchSize = 0; // 0 = infer from profile (b1/b8/b16)
    std::size_t intraOpThreads = 0; // 0 = ONNX Runtime default
    PoseOutputMode outputMode = PoseOutputMode::Auto;
};

class PoseEstimator {
public:
    PoseEstimator(std::string modelPath, PoseEstimatorParams params = {});
    ~PoseEstimator();

    // Creates/validates the ORT session without running a frame. Useful for
    // showing a truthful "initializing accelerator" stage before video decode.
    // Repeated calls are cheap because processSessionCache() owns the session.
    void prepare();

    std::vector<PoseDetection> infer(const PoseImage& image);
    std::vector<std::vector<PoseDetection>> inferBatch(const std::vector<PoseImage>& images);
    const std::string& modelPath() const noexcept { return modelPath_; }
    const PoseEstimatorParams& params() const noexcept { return params_; }

private:
    std::string modelPath_;
    PoseEstimatorParams params_;
    SessionHandle acquireSession();
    std::optional<SessionSpec> sessionSpec_;
    std::string sessionSpecKey_;
    std::unique_ptr<detail::PoseInputWorkspace> inputWorkspace_;
    std::mutex mutex_;
};

} // namespace pfgpu
