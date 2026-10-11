#pragma once

#include <string>

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
