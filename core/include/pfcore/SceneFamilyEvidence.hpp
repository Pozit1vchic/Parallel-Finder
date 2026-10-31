#pragma once
#include <cstddef>
#include <string>
#include <vector>

namespace pfcore {
enum class SceneFamilyReason {
    Representative, OverlappingEvent, AdjacentEvent, CopiedFootage,
    InvalidRange, DifferentIdentity, DifferentScene, DifferentSequence,
    DifferentContext, DistantOccurrence, MissingPose, DifferentPose,
    DifferentAction, DifferentDirection, DifferentPosture
};
inline const char* sceneFamilyReasonCode(SceneFamilyReason reason) noexcept {
    switch(reason) {
    case SceneFamilyReason::Representative:return "representative";
    case SceneFamilyReason::OverlappingEvent:return "overlapping_event";
    case SceneFamilyReason::AdjacentEvent:return "adjacent_event";
    case SceneFamilyReason::CopiedFootage:return "copied_footage";
    case SceneFamilyReason::InvalidRange:return "invalid_range";
    case SceneFamilyReason::DifferentIdentity:return "different_identity";
    case SceneFamilyReason::DifferentScene:return "different_scene";
    case SceneFamilyReason::DifferentSequence:return "different_sequence";
    case SceneFamilyReason::DifferentContext:return "different_context";
    case SceneFamilyReason::DistantOccurrence:return "distant_occurrence";
    case SceneFamilyReason::MissingPose:return "missing_pose";
    case SceneFamilyReason::DifferentPose:return "different_pose";
    case SceneFamilyReason::DifferentAction:return "different_action";
    case SceneFamilyReason::DifferentDirection:return "different_direction";
    case SceneFamilyReason::DifferentPosture:return "different_posture";
    }
    return "invalid_range";
}
struct SceneFamilyComparison {
    bool accepted=false;
    SceneFamilyReason reason=SceneFamilyReason::InvalidRange;
    double temporalOverlap=-1,poseDistance=-1,contextSimilarity=-1;
};
struct SceneFamilyMemberEvidence {
    std::size_t windowIndex=0;
    double start=0,end=0;
    double sceneStart=-1,sceneEnd=-1;
    double representativeQuality=0,evidenceQuality=0,identityConfidence=0,temporalCoverage=0,matchSimilarity=0;
    bool headOnly=false,representative=false;
    SceneFamilyComparison relation;
};
// Shared once per family across result pairs, without retaining video frames
// or pose/biometric arrays. Members are unique observed interval/scope variants.
struct SceneFamilyEvidence {
    std::string canonicalId,sourceId;
    std::size_t identityId=0,rawOccurrences=0;
    double start=0,end=0,sceneStart=-1,sceneEnd=-1,representativeQuality=0;
    std::vector<SceneFamilyMemberEvidence> members;
};
}
