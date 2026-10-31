#pragma once
#include "pfcore/MotionMatcher.hpp"
#include <algorithm>
#include <cmath>
#include <optional>
#include <span>

namespace pfcore {
struct DetectorOutputReplay {
    std::size_t candidate=0;
    double maximumPointError=0,maximumConfidenceError=0;
};
// Recover the bounding box of an already selected detector observation by
// replaying the SAME model/settings/source pixels/timestamp. All 17 raw triples
// participate, including low-confidence coordinates as a numerical fingerprint,
// not as observed anatomical landmarks. This does not establish actor identity
// across frames and must never replace an articulated-pose quality measure.
inline std::optional<DetectorOutputReplay> associateDetectorOutputReplay(const PoseFrame& observed,
    std::span<const PoseFrame> candidates,int width,int height) {
    if(width<=0||height<=0||!std::isfinite(observed.timestampSeconds)||observed.keypoints.size()!=17)return {};
    const auto valid=[&](const Keypoint& p) {
        return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.confidence)&&p.confidence>=0&&p.confidence<=1
            &&p.x>=-double(width)&&p.x<=2.*width&&p.y>=-double(height)&&p.y<=2.*height;
    };
    if(!std::all_of(observed.keypoints.begin(),observed.keypoints.end(),valid))return {};
    double spread=0;
    for(const auto& p:observed.keypoints)spread=std::max(spread,std::hypot(p.x-observed.keypoints.front().x,p.y-observed.keypoints.front().y));
    if(spread<32)return {};
    const double diagonal=std::hypot(double(width),double(height));
    std::vector<DetectorOutputReplay> eligible;
    for(std::size_t index=0;index<candidates.size();++index) {
        const auto& fresh=candidates[index];
        if(fresh.keypoints.size()!=17||!std::isfinite(fresh.timestampSeconds)
            ||std::abs(fresh.timestampSeconds-observed.timestampSeconds)>1e-6
            ||!std::all_of(fresh.keypoints.begin(),fresh.keypoints.end(),valid))continue;
        DetectorOutputReplay r;r.candidate=index;
        for(std::size_t joint=0;joint<17;++joint) {
            const auto& a=observed.keypoints[joint];const auto& b=fresh.keypoints[joint];
            r.maximumPointError=std::max(r.maximumPointError,std::hypot(a.x-b.x,a.y-b.y)/diagonal);
            r.maximumConfidenceError=std::max(r.maximumConfidenceError,std::abs(a.confidence-b.confidence));
        }
        eligible.push_back(r);
    }
    if(eligible.empty())return {};
    std::sort(eligible.begin(),eligible.end(),[](const auto& a,const auto& b){return a.maximumPointError<b.maximumPointError;});
    const auto& best=eligible.front();
    if(best.maximumPointError>1e-4||best.maximumConfidenceError>1e-3)return {};
    // A near-duplicate output remains ambiguous even if its confidence differs.
    if(eligible.size()>1&&eligible[1].maximumPointError-best.maximumPointError<2e-3)return {};
    return best;
}
}
