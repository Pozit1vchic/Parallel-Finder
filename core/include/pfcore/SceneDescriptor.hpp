#pragma once
#include <pfcore/MovementClassifier.hpp>
#include <array>
#include <optional>

namespace pfcore {
enum class EvidenceLevel { Unknown, Ambiguous, Probable, Confident };
enum class ScenePosture { Unknown, Standing, Sitting, Lying, Crouching, Kneeling, SeatedOrLying };
enum class ViewDirection { Unknown, Front, QuarterLeft, SideLeft, BackQuarterLeft, Back, BackQuarterRight, SideRight, QuarterRight };
struct SceneOrientation {
    // Preserve measured rotation, including continuity across +/-180 degrees.
    std::optional<std::array<double,9>> rotation;
    std::optional<double> yawDegrees;
    std::optional<double> pitchDegrees;
    std::optional<double> rollDegrees;
    double confidence = 0;
};
struct SceneDescriptor {
    std::string sourceId;
    std::size_t identityId=0;
    std::optional<std::size_t> sceneId;
    std::optional<std::size_t> sequenceId;
    double start=0, end=0;
    ScenePosture posture=ScenePosture::Unknown;
    EvidenceLevel postureEvidence=EvidenceLevel::Unknown;
    SceneOrientation body, head;
    MovementClassification motion;
    std::array<double,34> normalizedSkeleton{};
    std::array<double,17> jointEvidence{};
    std::vector<float> sceneContext;
    std::array<double,2> imageVelocity{};
    double visibleCoverage=0;
    double poseConfidence=0;
    double identityConfidence=0;
    double temporalCoverage=0;
    double evidenceQuality=0;
    double headVisibleCoverage=0,headTemporalCoverage=0,headEvidenceQuality=0;
    bool cameraMotionKnown=false;
    // Missing camera information is not proof of a static camera.
    std::optional<double> cameraInfluence;
    EvidenceLevel bodyYawEvidence=EvidenceLevel::Unknown;
    std::size_t bodyYawObservedSamples=0,bodyYawRequestedSamples=0;
};
SceneDescriptor describeScene(const MotionWindow& window,double start,double end,std::size_t identityId=0);
ViewDirection viewDirection(double yawDegrees) noexcept;
double angularDistanceDegrees(double a,double b) noexcept;
// Missing or disjoint skeleton evidence does not produce a perfect similarity.
std::optional<double> scenePoseDistance(const SceneDescriptor& a,const SceneDescriptor& b);
std::optional<double> sceneHeadDistance(const SceneDescriptor& a,const SceneDescriptor& b);
double comparisonEvidenceQuality(const SceneDescriptor& a,const SceneDescriptor& b,bool headOnly);
std::optional<double> comparisonPoseSimilarity(const SceneDescriptor& a,const SceneDescriptor& b,bool headOnly);
}
