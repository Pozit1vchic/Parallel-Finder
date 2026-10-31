#pragma once
#include "pfgpu/PoseEstimator.hpp"
#include <optional>

namespace pfgpu {
struct BodyOrientationInput {
    FloatTensor tensor;
    int sourceWidth=0,sourceHeight=0;
    double scale=0;
    int padLeft=0,padTop=0;
    bool mirrored=false;
};
struct BodyOrientationDetection {
    double left=0,top=0,right=0,bottom=0;
    // Person detection confidence is NOT angle accuracy/confidence.
    double detectionScore=0,yawDegrees=0;
};
struct BodyOrientationRegion {double left=0,top=0,right=0,bottom=0;};
enum class BodyYawStatus {Observed,InvalidSubject,MissingDetection,AmbiguousAssociation,MirrorDisagreement};
inline const char* bodyYawStatusCode(BodyYawStatus status) noexcept {
    switch(status) {
    case BodyYawStatus::Observed:return "observed";
    case BodyYawStatus::InvalidSubject:return "invalid_subject";
    case BodyYawStatus::MissingDetection:return "missing_detection";
    case BodyYawStatus::AmbiguousAssociation:return "ambiguous_association";
    case BodyYawStatus::MirrorDisagreement:return "mirror_disagreement";
    }
    return "invalid_subject";
}
struct BodyYawObservation {
    BodyYawStatus status=BodyYawStatus::MissingDetection;
    std::optional<double> yawDegrees;
    double associationIou=-1,associationMargin=-1,mirrorDifferenceDegrees=-1;
    double detectionScore=-1;
};
// RGB / 255, fixed 1024 letterbox (114), integer top/left padding and bilinear
// byte resize. Mirrored results are mapped back to original image coordinates.
BodyOrientationInput prepareBodyOrientationInput(const PoseImage& image,bool mirrored=false);
std::vector<BodyOrientationDetection> decodeBodyOrientationOutput(const FloatTensor& output,const BodyOrientationInput& input);
// Mutual unique association prevents one detection from labelling two bodies.
// This is spatial + mirror support; temporal/crop support is a caller concern.
// No head angle or detector objectness is substituted for body orientation.
std::vector<BodyYawObservation> associateBodyOrientations(const std::vector<BodyOrientationRegion>& subjects,
    const std::vector<BodyOrientationDetection>& original,const std::vector<BodyOrientationDetection>& mirrored);
class BodyOrientationEstimator {
public:
    BodyOrientationEstimator(std::string modelPath,Provider provider,std::size_t intraOpThreads=0);
    std::vector<BodyOrientationDetection> infer(const PoseImage& image,bool mirrored=false);
private:
    std::string modelPath_;
    Provider provider_;
    std::size_t threads_;
};
}
