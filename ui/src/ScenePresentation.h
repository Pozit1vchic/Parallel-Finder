#pragma once
#include <pfcore/SceneDescriptor.hpp>
#include <QVariantMap>
#include <QVariantList>
#include <cmath>

namespace pfui {
inline QVariantMap sceneFamilyMap(const pfcore::SceneFamilyEvidence& family,double frameRate=24) {
    QVariantList members;
    for(const auto& member:family.members) {
        QVariantMap value{{"start",member.start},{"end",member.end},{"sceneStart",member.sceneStart},{"sceneEnd",member.sceneEnd},{"headOnly",member.headOnly},
            {"representative",member.representative},{"quality",member.representativeQuality},
            {"evidenceQuality",member.evidenceQuality},{"identityConfidence",member.identityConfidence},
            {"temporalCoverage",member.temporalCoverage},{"matchSimilarity",member.matchSimilarity},
            {"reason",QString::fromLatin1(pfcore::sceneFamilyReasonCode(member.relation.reason))}};
        if(member.relation.temporalOverlap>=0)value.insert("overlap",member.relation.temporalOverlap);
        if(member.relation.poseDistance>=0)value.insert("poseDistance",member.relation.poseDistance);
        if(member.relation.contextSimilarity>=0)value.insert("contextSimilarity",member.relation.contextSimilarity);
        members.push_back(value);
    }
    return {{"id",QString::fromStdString(family.canonicalId)},{"source",QString::fromStdString(family.sourceId)},
        {"identityId",QString::number(family.identityId)},{"rawOccurrences",static_cast<qulonglong>(family.rawOccurrences)},
        {"start",family.start},{"end",family.end},{"sceneStart",family.sceneStart},{"sceneEnd",family.sceneEnd},
        {"quality",family.representativeQuality},{"frameRate",frameRate},{"members",members}};
}
inline QVariantMap sceneDescriptorMap(const pfcore::SceneDescriptor& d) {
    QVariantMap result{{"source",QString::fromStdString(d.sourceId)},{"start",d.start},{"end",d.end},
        {"posture",static_cast<int>(d.posture)},{"postureEvidence",static_cast<int>(d.postureEvidence)},
        {"visibility",d.visibleCoverage},{"poseConfidence",d.poseConfidence},
        {"temporalCoverage",d.temporalCoverage},{"quality",d.evidenceQuality}};
    if(d.head.yawDegrees) {
        result.insert("headYaw",*d.head.yawDegrees);
        result.insert("headView",static_cast<int>(pfcore::viewDirection(*d.head.yawDegrees)));
        result.insert("headYawStrength",d.head.confidence);
    }
    if(d.head.pitchDegrees)result.insert("headPitch",*d.head.pitchDegrees);
    if(d.head.rollDegrees)result.insert("headRoll",*d.head.rollDegrees);
    result.insert("bodyYawEvidence",static_cast<int>(d.bodyYawEvidence));
    result.insert("bodyYawObservedSamples",static_cast<qulonglong>(d.bodyYawObservedSamples));
    result.insert("bodyYawRequestedSamples",static_cast<qulonglong>(d.bodyYawRequestedSamples));
    if(d.body.yawDegrees) {
        result.insert("bodyYaw",*d.body.yawDegrees);result.insert("bodyView",static_cast<int>(pfcore::viewDirection(*d.body.yawDegrees)));
        result.insert("bodyYawStrength",d.body.confidence);
    }
    if(d.body.rollDegrees)result.insert("bodyRoll",*d.body.rollDegrees);
    if(d.sceneId)result.insert("scene",static_cast<qulonglong>(*d.sceneId));
    return result;
}
inline void updateSceneEvidence(QVariantMap& row,pfcore::MotionMatch& match,
    const pfcore::MotionWindow& left,const pfcore::MotionWindow& right) {
    const auto a=pfcore::describeScene(left,match.leftStartSeconds,match.leftEndSeconds);
    const auto b=pfcore::describeScene(right,match.rightStartSeconds,match.rightEndSeconds);
    match.evidenceQuality=pfcore::comparisonEvidenceQuality(a,b,match.headOnlyComparison);
    match.poseSimilarity=pfcore::comparisonPoseSimilarity(a,b,match.headOnlyComparison).value_or(-1.);
    match.bestMatchScore=match.similarity*(.55+.45*match.evidenceQuality)
        *((match.faceVerified||match.appearanceVerified)?1.:.85);
    row.insert("leftDescriptor",sceneDescriptorMap(a));row.insert("rightDescriptor",sceneDescriptorMap(b));
    row.insert("evidenceQuality",match.evidenceQuality);row.insert("poseSimilarity",match.poseSimilarity);
    row.insert("bestMatchScore",match.bestMatchScore);
    row.insert("evidenceScope",match.headOnlyComparison?QStringLiteral("head"):QStringLiteral("body"));
    row.insert("visibilityCoverage",match.headOnlyComparison?std::min(a.headVisibleCoverage,b.headVisibleCoverage):std::min(a.visibleCoverage,b.visibleCoverage));
    row.insert("temporalCoverage",match.headOnlyComparison?std::min(a.headTemporalCoverage,b.headTemporalCoverage):std::min(a.temporalCoverage,b.temporalCoverage));
    row.insert("matchTier",match.evidenceQuality>=.45&&(match.headOnlyComparison?match.faceVerified:(match.faceVerified||match.appearanceVerified))
        ? QStringLiteral("strong"):QStringLiteral("possible"));
}
}
