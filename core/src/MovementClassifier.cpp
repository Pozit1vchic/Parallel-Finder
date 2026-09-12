#include "pfcore/MovementClassifier.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace pfcore {
namespace {

struct Point {
    double x = 0.0;
    double y = 0.0;
};

Point centroid(const PoseFrame& frame)
{
    if (frame.keypoints.empty()) return {};
    Point result;
    double weight = 0.0;
    for (const auto& point : frame.keypoints) {
        const double confidence = std::max(0.0, point.confidence);
        result.x += point.x * confidence;
        result.y += point.y * confidence;
        weight += confidence;
    }
    if (weight <= 1e-9) weight = static_cast<double>(frame.keypoints.size());
    return {result.x / weight, result.y / weight};
}

double bodyScale(const PoseFrame& frame)
{
    if (frame.keypoints.empty()) return 1.0;
    double left = std::numeric_limits<double>::infinity();
    double top = std::numeric_limits<double>::infinity();
    double right = -std::numeric_limits<double>::infinity();
    double bottom = -std::numeric_limits<double>::infinity();
    for (const auto& point : frame.keypoints) {
        left = std::min(left, point.x); top = std::min(top, point.y);
        right = std::max(right, point.x); bottom = std::max(bottom, point.y);
    }
    return std::max(1e-9, std::hypot(right - left, bottom - top));
}

double keypointDelta(const PoseFrame& first, const PoseFrame& last, std::size_t index,
                     bool yAxis)
{
    if (index >= first.keypoints.size() || index >= last.keypoints.size()) return 0.0;
    const auto& a = first.keypoints[index];
    const auto& b = last.keypoints[index];
    return (yAxis ? b.y - a.y : b.x - a.x) / std::max(bodyScale(first), bodyScale(last));
}

MovementClassification classifyOne(const MotionWindow& window)
{
    MovementClassification result;
    if (window.frames.size() < 2) return result;
    const auto& first = window.frames.front();
    const auto& last = window.frames.back();
    const Point a = centroid(first), b = centroid(last);
    const double scale = std::max(bodyScale(first), bodyScale(last));
    const double dx = (b.x - a.x) / scale;
    const double dy = (b.y - a.y) / scale;
    const double displacement = std::hypot(dx, dy);
    const double scaleChange = (bodyScale(last) - bodyScale(first)) / scale;

    if (std::abs(scaleChange) >= 0.12) {
        result.direction = scaleChange > 0.0 ? MovementDirection::TowardCamera
                                             : MovementDirection::AwayFromCamera;
        result.directionConfidence = std::clamp(std::abs(scaleChange) / 0.35, 0.0, 1.0);
    } else if (displacement < 0.035) {
        result.direction = MovementDirection::Static;
        result.directionConfidence = 1.0 - std::clamp(displacement / 0.035, 0.0, 1.0);
    } else if (std::abs(dx) >= std::abs(dy) * 1.15) {
        result.direction = dx < 0.0 ? MovementDirection::Left : MovementDirection::Right;
        result.directionConfidence = std::clamp(std::abs(dx) / 0.35, 0.0, 1.0);
    } else {
        result.direction = MovementDirection::Mixed;
        result.directionConfidence = std::clamp(displacement / 0.35, 0.0, 1.0);
    }

    // COCO order: left/right shoulder 5/6 and wrist 9/10. These heuristics are
    // deliberately transparent and deterministic; no second classifier model
    // is smuggled into the pose-only v1 pipeline.
    const double leftWristY = keypointDelta(first, last, 9, true);
    const double rightWristY = keypointDelta(first, last, 10, true);
    const double wristLift = -(leftWristY + rightWristY) * 0.5;
    const double wristSpread = std::abs(keypointDelta(first, last, 9, false))
        + std::abs(keypointDelta(first, last, 10, false));
    const double shoulderTurn = std::abs(keypointDelta(first, last, 5, false)
                                         - keypointDelta(first, last, 6, false));
    if (wristLift > 0.10) {
        result.gesture = GestureClass::Raise;
        result.gestureConfidence = std::clamp(wristLift / 0.30, 0.0, 1.0);
    } else if (wristLift < -0.10) {
        result.gesture = GestureClass::Lower;
        result.gestureConfidence = std::clamp(-wristLift / 0.30, 0.0, 1.0);
    } else if (wristSpread > 0.22 && displacement < 0.18) {
        result.gesture = GestureClass::Wave;
        result.gestureConfidence = std::clamp(wristSpread / 0.6, 0.0, 1.0);
    } else if (shoulderTurn > 0.20) {
        result.gesture = GestureClass::Turn;
        result.gestureConfidence = std::clamp(shoulderTurn / 0.6, 0.0, 1.0);
    } else if (displacement > 0.10) {
        result.gesture = GestureClass::Step;
        result.gestureConfidence = std::clamp(displacement / 0.35, 0.0, 1.0);
    } else {
        result.gesture = displacement < 0.035 ? GestureClass::Static : GestureClass::Unknown;
        result.gestureConfidence = result.gesture == GestureClass::Static ? 1.0 : 0.0;
    }
    return result;
}

template <typename T>
T combine(T left, T right, double leftConfidence, double rightConfidence, T mixed)
{
    if (left == right) return left;
    if (leftConfidence <= 0.0) return right;
    if (rightConfidence <= 0.0) return left;
    return mixed;
}

} // namespace

MovementClassification MovementClassifier::classify(const MotionWindow& window) const
{
    return classifyOne(window);
}

MovementClassification MovementClassifier::classify(const MotionWindow& left,
                                                    const MotionWindow& right) const
{
    const auto a = classifyOne(left);
    const auto b = classifyOne(right);
    MovementClassification result;
    result.direction = combine(a.direction, b.direction, a.directionConfidence,
                               b.directionConfidence, MovementDirection::Mixed);
    result.gesture = combine(a.gesture, b.gesture, a.gestureConfidence,
                             b.gestureConfidence, GestureClass::Mixed);
    result.directionConfidence = std::min(a.directionConfidence, b.directionConfidence);
    result.gestureConfidence = std::min(a.gestureConfidence, b.gestureConfidence);
    return result;
}

const char* MovementClassifier::directionName(MovementDirection value) noexcept
{
    switch (value) {
    case MovementDirection::TowardCamera: return "toward_camera";
    case MovementDirection::AwayFromCamera: return "away_from_camera";
    case MovementDirection::Left: return "left";
    case MovementDirection::Right: return "right";
    case MovementDirection::Static: return "static";
    case MovementDirection::Mixed: return "mixed";
    case MovementDirection::Unknown: break;
    }
    return "unknown";
}

const char* MovementClassifier::gestureName(GestureClass value) noexcept
{
    switch (value) {
    case GestureClass::Push: return "push";
    case GestureClass::Pull: return "pull";
    case GestureClass::Raise: return "raise";
    case GestureClass::Lower: return "lower";
    case GestureClass::Turn: return "turn";
    case GestureClass::Step: return "step";
    case GestureClass::Wave: return "wave";
    case GestureClass::Static: return "static";
    case GestureClass::Mixed: return "mixed";
    case GestureClass::Unknown: break;
    }
    return "unknown";
}

} // namespace pfcore
