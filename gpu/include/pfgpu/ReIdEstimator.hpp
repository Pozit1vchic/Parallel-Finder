#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "pfgpu/Inference.hpp"

namespace pfgpu {

// A person crop is kept as a view over the decoder's RGBA frame.  The
// estimator owns no image memory and must therefore be called while the frame
// buffer is alive.
struct ReIdImage {
    int width = 0;
    int height = 0;
    const std::uint8_t* rgba = nullptr;
    float left = 0.0F;
    float top = 0.0F;
    float right = 0.0F;
    float bottom = 0.0F;
};

struct ReIdEstimatorParams {
    // OSNet/FastReID exports conventionally use NCHW 3x256x128 input.
    int inputWidth = 128;
    int inputHeight = 256;
    Provider provider = Provider::Auto;
    std::string profile = "b1";
    std::size_t intraOpThreads = 0;
};

class ReIdEstimator {
public:
    ReIdEstimator(std::string modelPath, ReIdEstimatorParams params = {});

    // Returns one L2-normalized embedding.  An invalid/empty crop is rejected
    // rather than represented as an all-zero identity vector.
    std::vector<float> infer(const ReIdImage& image);
    std::vector<std::vector<float>> inferBatch(const std::vector<ReIdImage>& images);

    const std::string& modelPath() const noexcept { return modelPath_; }
    const ReIdEstimatorParams& params() const noexcept { return params_; }

private:
    std::string modelPath_;
    ReIdEstimatorParams params_;
    SessionCache sessions_;
    std::optional<SessionSpec> sessionSpec_;
    bool channelsFirst_ = true;
};

} // namespace pfgpu
