#pragma once
#include <pfcore/SceneDescriptor.hpp>
#include <QVariantMap>
#include <cmath>

namespace pfui {
inline QVariantMap sceneDescriptorMap(const pfcore::SceneDescriptor& d) {
    QVariantMap result{{"source",QString::fromStdString(d.sourceId)},{"start",d.start},{"end",d.end},
        {"posture",static_cast<int>(d.posture)},{"postureEvidence",static_cast<int>(d.postureEvidence)},
        {"visibility",d.visibleCoverage},{"poseConfidence",d.poseConfidence},
        {"temporalCoverage",d.temporalCoverage},{"quality",d.evidenceQuality}};
    if(d.head.yawDegrees)result.insert("headYaw",*d.head.yawDegrees);
    if(d.head.pitchDegrees)result.insert("headPitch",*d.head.pitchDegrees);
    if(d.body.yawDegrees)result.insert("bodyYaw",*d.body.yawDegrees);
    if(d.sceneId)result.insert("scene",static_cast<qulonglong>(*d.sceneId));
    return result;
}
inline void updateSceneEvidence(QVariantMap& row,pfcore::MotionMatch& match,
    const pfcore::MotionWindow& left,const pfcore::MotionWindow& right) {
    const auto a=pfcore::describeScene(left,match.leftStartSeconds,match.leftEndSeconds);
    const auto b=pfcore::describeScene(right,match.rightStartSeconds,match.rightEndSeconds);
    match.evidenceQuality=std::min(a.evidenceQuality,b.evidenceQuality);
    if(const auto distance=pfcore::scenePoseDistance(a,b))match.poseSimilarity=std::exp(-*distance);
    match.bestMatchScore=match.similarity*(.55+.45*match.evidenceQuality)
        *((match.faceVerified||match.appearanceVerified)?1.:.85);
    row.insert("leftDescriptor",sceneDescriptorMap(a));row.insert("rightDescriptor",sceneDescriptorMap(b));
    row.insert("evidenceQuality",match.evidenceQuality);row.insert("poseSimilarity",match.poseSimilarity);
    row.insert("bestMatchScore",match.bestMatchScore);
    row.insert("visibilityCoverage",std::min(a.visibleCoverage,b.visibleCoverage));
    row.insert("temporalCoverage",std::min(a.temporalCoverage,b.temporalCoverage));
    row.insert("matchTier",match.evidenceQuality>=.45&&(match.faceVerified||match.appearanceVerified)
        ? QStringLiteral("strong"):QStringLiteral("possible"));
}
}
