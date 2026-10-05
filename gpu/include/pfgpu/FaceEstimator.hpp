#pragma once
#include <array>
#include "pfgpu/ReIdEstimator.hpp"

namespace pfgpu {
// Optional audit output; unset/invalid when no recognition crop was accepted.
// Canonical-to-source maps the 112x112 recognition image into source pixels.
struct FaceRecognitionDiagnostics {
    bool accepted = false;
    double detectorScore = 0;
    std::array<double, 10> landmarks{};
    std::array<double, 6> canonicalToSource{};
};
// YuNet five-landmark detection followed by aligned SFace recognition.
// Empty output means no unambiguous, sufficiently large face was observed.
class FaceEstimator {
public:
    FaceEstimator(std::string detector, std::string recognizer, Provider provider);
    std::vector<float> infer(const ReIdImage& person, FaceRecognitionDiagnostics* diagnostics = nullptr,
                             double minimumDetectionConfidence = .85,
                             const std::array<double,2>* expectedNose = nullptr);
    // Optional verification from measured right-eye, left-eye and nose pixels.
    // No detector identity claim: callers must verify the returned descriptor.
    std::vector<float> inferFromPose(const ReIdImage& image,
        const std::array<double,6>& eyesAndNose,FaceRecognitionDiagnostics* diagnostics = nullptr);
private:
    std::string detector_, recognizer_;
    Provider provider_;
};
}
