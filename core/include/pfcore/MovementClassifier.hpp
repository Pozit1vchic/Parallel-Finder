#pragma once

#include <string>
#include <vector>

#include "pfcore/MotionMatcher.hpp"

namespace pfcore {

enum class MovementDirection {
    Unknown,
    TowardCamera,
    AwayFromCamera,
    Left,
    Right,
    Static,
    Mixed,
};

enum class GestureClass {
    Unknown,
    Push,
    Pull,
    Raise,
    Lower,
    Turn,
    Step,
    Wave,
    Static,
    Mixed,
    RaiseLower,
    ReachReturn,
    HeadTurn,
    HeadNod,
    HeadTilt,
};

struct MovementClassification {
    MovementDirection direction = MovementDirection::Unknown;
    GestureClass gesture = GestureClass::Unknown;
    double directionConfidence = 0.0;
    double gestureConfidence = 0.0;
    // True when the movement category is supported only by head evidence,
    // with body motion unavailable in the selected interval.
    bool headOnlyEvidence = false;
    // Atomic movement evidence retained when more than one action overlaps.
    // This gives result grouping a stable shared facet instead of putting all
    // composite movements into one broad "mixed" bucket.
    std::vector<GestureClass> actions;
    // Both sides have supported actions, but share no action facet. This is
    // different from missing evidence and must not become an Unknown bucket.
    bool conflictingActions = false;
};

class MovementClassifier {
public:
    // Conservative temporal grouping, independent of the established matcher
    // admission rules below. Confidence is evidence strength, not a calibrated
    // probability. Unobservable/ambiguous actions remain Unknown.
    [[nodiscard]] MovementClassification classifyForGrouping(const MotionWindow& window,
        double start, double end, bool headOnly=false) const;
    [[nodiscard]] MovementClassification classifyGeometricForGrouping(const MotionWindow& window,
        double start, double end, bool headOnly=false) const;
    [[nodiscard]] std::string classifyHeadTurnForGrouping(const MotionWindow& window,
        double start, double end) const;
    [[nodiscard]] static MovementClassification combineForGrouping(
        const MovementClassification& left, const MovementClassification& right);
    [[nodiscard]] std::string classifyPoseForGrouping(const MotionWindow& window,
        double start, double end, bool headOnly) const;
    // Conservative whole-body posture grouping from visible torso/leg joints.
    // Sitting and lying intentionally share one bucket; missing body evidence
    // is reported as unknown instead of being guessed as standing.
    [[nodiscard]] std::string classifyPostureForGrouping(const MotionWindow& window,
        double start, double end) const;
    [[nodiscard]] MovementClassification classify(const MotionWindow& window) const;
    [[nodiscard]] MovementClassification classify(const MotionWindow& window, double start, double end) const;
    [[nodiscard]] MovementClassification classify(const MotionWindow& left,
                                                  const MotionWindow& right) const;
    [[nodiscard]] static MovementClassification combineClassifications(
        const MovementClassification& left, const MovementClassification& right);

    static const char* directionName(MovementDirection value) noexcept;
    static const char* gestureName(GestureClass value) noexcept;
};

} // namespace pfcore
