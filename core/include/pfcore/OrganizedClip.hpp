#pragma once
#include <pfcore/MotionMatcher.hpp>
#include <pfcore/ParallelClip.hpp>

namespace pfcore {
struct OrganizedClip {
    ParallelClipRange range;
    double sceneEnd=-1;
};
// A canonical representative may belong to a different copied shot. Its own
// boundaries must travel with it; the original comparison's shot is not its cut.
inline OrganizedClip organizedClip(const MotionMatch& match,bool left,double framesPerSecond=24.,bool manuallyAdjusted=false) {
    const double start=left?match.leftStartSeconds:match.rightStartSeconds;
    const double end=left?match.leftEndSeconds:match.rightEndSeconds;
    const double sceneStart=left?match.leftSceneStartSeconds:match.rightSceneStartSeconds;
    const double sceneEnd=left?match.leftSceneEndSeconds:match.rightSceneEndSeconds;
    if(manuallyAdjusted)return {{start,end},sceneEnd};
    const auto& family=left?match.leftFamilyEvidence:match.rightFamilyEvidence;
    const auto& source=left?match.leftSourceId:match.rightSourceId;
    const auto& familyId=left?match.leftSceneFamily:match.rightSceneFamily;
    if(family && !family->canonicalId.empty() && family->sourceId==source
        &&family->canonicalId==familyId
        &&std::isfinite(family->start)&&std::isfinite(family->end)&&family->start>=0&&family->end>family->start)
        return {parallelSceneClipRange(family->start,family->end,family->sceneStart,family->sceneEnd,framesPerSecond),family->sceneEnd};
    if(family)return {parallelSceneClipRange(start,end,sceneStart,sceneEnd,framesPerSecond),sceneEnd};
    const double canonicalStart=left?match.leftCanonicalStart:match.rightCanonicalStart;
    const double canonicalEnd=left?match.leftCanonicalEnd:match.rightCanonicalEnd;
    return {parallelSceneClipRange(canonicalStart>=0?canonicalStart:start,canonicalEnd>=0?canonicalEnd:end,
        sceneStart,sceneEnd,framesPerSecond),sceneEnd};
}
}
