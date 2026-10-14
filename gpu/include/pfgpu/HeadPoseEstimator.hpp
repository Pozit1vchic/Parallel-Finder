#pragma once
#include <array>
#include <optional>
#include "pfgpu/ReIdEstimator.hpp"

namespace pfgpu {
// The crop is an unaligned, integer-bounded RGB face region, including the
// author's margin. It must be associated with the tracked person beforehand.
[[nodiscard]] FloatTensor prepareHeadPoseInput(const ReIdImage& faceCrop);

class HeadPoseEstimator {
public:
    HeadPoseEstimator(std::string modelPath, Provider provider);
    [[nodiscard]] std::optional<std::array<double,9>> infer(const ReIdImage& faceCrop);
private:
    std::string modelPath_;
    Provider provider_;
};
}
