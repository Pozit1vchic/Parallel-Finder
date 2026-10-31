#pragma once
#include <cmath>
#include <optional>
#include <string>
#include <vector>

namespace pfcore {
inline constexpr char kBodyOrientationModelHash[]="3bdfefba3f0bbcd23454ceefaa3088c2e95a7b9d120502899f65249ae98e1c93";
struct BodyOrientationSample {
    std::string sourceId,modelHash,reason;
    double time=0,yawDegrees=0;
    double associationIou=-1,associationMargin=-1,mirrorDifference=-1,cropDifference=-1;
    bool observed=false,poseAssociated=false;
    friend bool operator==(const BodyOrientationSample&,const BodyOrientationSample&)=default;
};
inline bool validBodyOrientationSample(const BodyOrientationSample& sample,const std::string& source) {
    return sample.observed&&sample.poseAssociated&&!source.empty()&&sample.sourceId==source&&sample.modelHash==kBodyOrientationModelHash
        &&std::isfinite(sample.time)&&sample.time>=0&&std::isfinite(sample.yawDegrees)&&std::abs(sample.yawDegrees)<=180
        &&std::isfinite(sample.associationIou)&&sample.associationIou>=.5&&sample.associationIou<=1
        &&std::isfinite(sample.associationMargin)&&sample.associationMargin>=.15&&sample.associationMargin<=1
        &&std::isfinite(sample.mirrorDifference)&&sample.mirrorDifference>=0&&sample.mirrorDifference<=25
        &&std::isfinite(sample.cropDifference)&&sample.cropDifference>=0&&sample.cropDifference<=20;
}
struct BodyYawSummary {
    std::optional<double> yawDegrees;
    double evidenceStrength=0;
    std::size_t observed=0,total=0;
    bool ambiguous=false;
};
BodyYawSummary summarizeBodyYaw(const std::vector<BodyOrientationSample>& samples,const std::string& source,double start,double end);
}
