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
    // Independently observed actions. Empty means the broad class cannot
    // safely be decomposed; it must not invent facets for ambiguous motion.
    std::vector<GestureClass> actions;
    // A measured head action retained when body joints are unobservable.
    // It does not claim that the rest of the scene contains no other action.
    bool headOnlyEvidence = false;
};

class MovementClassifier {
public:
    // Conservative temporal grouping, independent of the established matcher
    // admission rules below. Confidence is evidence strength, not a calibrated
    // probability. Unobservable/ambiguous actions remain Unknown.
    [[nodiscard]] MovementClassification classifyForGrouping(const MotionWindow& window,
        double start, double end, bool headOnly=false) const;
    [[nodiscard]] static MovementClassification combineForGrouping(
        const MovementClassification& left, const MovementClassification& right);
    [[nodiscard]] std::string classifyPoseForGrouping(const MotionWindow& window,
        double start, double end, bool headOnly) const;
    [[nodiscard]] std::string classifyHeadTurnForGrouping(const MotionWindow& window,
        double start, double end) const;
    [[nodiscard]] MovementClassification classify(const MotionWindow& window) const;
    [[nodiscard]] MovementClassification classify(const MotionWindow& window, double start, double end) const;
    [[nodiscard]] MovementClassification classify(const MotionWindow& left,
                                                  const MotionWindow& right) const;
    [[nodiscard]] static MovementClassification combineClassifications(
        const MovementClassification& left, const MovementClassification& right);

    static const char* directionName(MovementDirection value) noexcept;
    static const char* gestureName(GestureClass value) noexcept;
private:
    MovementClassification classifyGeometricForGrouping(const MotionWindow& window,
        double start,double end,bool headOnly) const;
};

} // namespace pfcore
