#pragma once
#include "pfgpu/ReIdEstimator.hpp"

namespace pfgpu {
// YuNet five-landmark detection followed by aligned SFace recognition.
// Empty output means no unambiguous, sufficiently large face was observed.
class FaceEstimator {
public:
    FaceEstimator(std::string detector, std::string recognizer, Provider provider);
    std::vector<float> infer(const ReIdImage& person);
private:
    std::string detector_, recognizer_;
    Provider provider_;
    SessionCache sessions_;
};
}
