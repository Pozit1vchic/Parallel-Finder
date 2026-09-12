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
};

struct MovementClassification {
    MovementDirection direction = MovementDirection::Unknown;
    GestureClass gesture = GestureClass::Unknown;
    double directionConfidence = 0.0;
    double gestureConfidence = 0.0;
};

class MovementClassifier {
public:
    [[nodiscard]] MovementClassification classify(const MotionWindow& window) const;
    [[nodiscard]] MovementClassification classify(const MotionWindow& left,
                                                  const MotionWindow& right) const;

    static const char* directionName(MovementDirection value) noexcept;
    static const char* gestureName(GestureClass value) noexcept;
};

} // namespace pfcore
