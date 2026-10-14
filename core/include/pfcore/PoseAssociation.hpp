#pragma once
#include "pfcore/MotionMatcher.hpp"
#include <algorithm>
#include <cmath>
#include <optional>
#include <span>

namespace pfcore {
struct PoseAssociationParams {
    // Geometry tolerances for replaying the same model at the SAME source
    // timestamp. They are not calibrated identity probabilities.
    double maximumMedianError = .001; // fraction of image diagonal
    double minimumRunnerUpMargin = .0005;
    double minimumCoverage = .8;
    std::size_t minimumJoints = 6;
};
struct PoseAssociation {
    std::size_t candidate = 0;
    std::size_t commonJoints = 0;
    double normalizedMedianError = 0;
};
// Select a spatially corresponding detection for local cache repair, never
// prove actor identity or action similarity. The caller supplies frames in
// one image coordinate system, with separately verified source/model keys.
inline std::optional<PoseAssociation> associateObservedPose(const PoseFrame& observed,
    std::span<const PoseFrame> candidates,int width,int height,
    const PoseAssociationParams& params={})
{
    if(width<=0||height<=0||!std::isfinite(observed.timestampSeconds)
        ||observed.keypoints.size()!=17||params.minimumJoints<6||params.minimumJoints>17
        ||!std::isfinite(params.maximumMedianError)||params.maximumMedianError<=0
        ||!std::isfinite(params.minimumRunnerUpMargin)||params.minimumRunnerUpMargin<=0
        ||!std::isfinite(params.minimumCoverage)||params.minimumCoverage<=0||params.minimumCoverage>1)return {};
    const auto visible=[&](const Keypoint& p) {
        return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.confidence)
            &&p.confidence>=.5&&p.confidence<=1&&p.x>=0&&p.x<width&&p.y>=0&&p.y<height;
    };
    const auto observedJoints=std::count_if(observed.keypoints.begin(),observed.keypoints.end(),visible);
    if(observedJoints<static_cast<std::ptrdiff_t>(params.minimumJoints))return {};
    const double scale=std::hypot(double(width),double(height));
    std::vector<PoseAssociation> comparable;
    for(std::size_t index=0;index<candidates.size();++index) {
        const auto& fresh=candidates[index];
        if(!std::isfinite(fresh.timestampSeconds)||std::abs(fresh.timestampSeconds-observed.timestampSeconds)>1e-6
            ||fresh.keypoints.size()!=observed.keypoints.size())continue;
        std::vector<double> errors;
        for(std::size_t joint=0;joint<17;++joint)if(visible(observed.keypoints[joint])&&visible(fresh.keypoints[joint]))
            errors.push_back(std::hypot(observed.keypoints[joint].x-fresh.keypoints[joint].x,
                observed.keypoints[joint].y-fresh.keypoints[joint].y)/scale);
        if(errors.size()<params.minimumJoints||errors.size()<params.minimumCoverage*observedJoints)continue;
        std::sort(errors.begin(),errors.end());
        // A matching face cannot hide strongly disagreeing visible limbs.
        if(errors[static_cast<std::size_t>(std::ceil(.9*errors.size()))-1]>4*params.maximumMedianError)continue;
        const double median=errors.size()%2 ? errors[errors.size()/2]
            : (errors[errors.size()/2-1]+errors[errors.size()/2])/2;
        comparable.push_back({index,errors.size(),median});
    }
    if(comparable.empty())return {};
    std::stable_sort(comparable.begin(),comparable.end(),[](const auto& a,const auto& b) {
        return a.normalizedMedianError<b.normalizedMedianError;
    });
    if(comparable.front().normalizedMedianError>params.maximumMedianError)return {};
    if(comparable.size()>1&&comparable[1].normalizedMedianError-comparable[0].normalizedMedianError
        <params.minimumRunnerUpMargin)return {};
    return comparable.front();
}
}
