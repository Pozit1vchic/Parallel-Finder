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

double keypointDelta(const PoseFrame& first, const PoseFrame& last, std::size_t index,
                     bool yAxis, double scale)
{
    if (index >= first.keypoints.size() || index >= last.keypoints.size()) return 0.0;
    const auto& a = first.keypoints[index];
    const auto& b = last.keypoints[index];
    return (yAxis ? b.y - a.y : b.x - a.x) / scale;
}

bool observedAtBothEnds(const PoseFrame& first, const PoseFrame& last, std::size_t index)
{
    if (index >= first.keypoints.size() || index >= last.keypoints.size()) return false;
    const auto valid = [](const auto& point) {
        return std::isfinite(point.x) && std::isfinite(point.y)
            && std::isfinite(point.confidence) && point.confidence >= 0.3;
    };
    return valid(first.keypoints[index]) && valid(last.keypoints[index]);
}

MovementClassification classifyEndpoints(const PoseFrame& first, const PoseFrame& last)
{
    MovementClassification result;
    // Compare the same observed landmarks at both ends. A missing limb must
    // not shift the centroid or stretch the body box as its confidence fades.
    Point a, b;
    constexpr double infinity = std::numeric_limits<double>::infinity();
    double leftA=infinity, topA=infinity, rightA=-infinity, bottomA=-infinity;
    double leftB=infinity, topB=infinity, rightB=-infinity, bottomB=-infinity;
    double weight=0;
    std::size_t count=0;
    for (std::size_t i=0; i<std::min(first.keypoints.size(),last.keypoints.size()); ++i) {
        if (!observedAtBothEnds(first,last,i)) continue;
        const auto& x=first.keypoints[i]; const auto& y=last.keypoints[i];
        const double w=std::min(x.confidence,y.confidence);
        a.x+=x.x*w; a.y+=x.y*w; b.x+=y.x*w; b.y+=y.y*w; weight+=w; ++count;
        leftA=std::min(leftA,x.x); rightA=std::max(rightA,x.x);
        topA=std::min(topA,x.y); bottomA=std::max(bottomA,x.y);
        leftB=std::min(leftB,y.x); rightB=std::max(rightB,y.x);
        topB=std::min(topB,y.y); bottomB=std::max(bottomB,y.y);
    }
    if (count < 3 || weight <= 0) return result;
    a.x/=weight; a.y/=weight; b.x/=weight; b.y/=weight;
    const double scaleA=std::hypot(rightA-leftA,bottomA-topA);
    const double scaleB=std::hypot(rightB-leftB,bottomB-topB);
    const double scale=std::max(scaleA,scaleB);
    if (!std::isfinite(scale) || scale < 1e-9) return result;
    const double dx = (b.x - a.x) / scale;
    const double dy = (b.y - a.y) / scale;
    const double displacement = std::hypot(dx, dy);
    const double scaleChange = (scaleB - scaleA) / scale;

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
    const bool leftWrist = observedAtBothEnds(first, last, 9);
    const bool rightWrist = observedAtBothEnds(first, last, 10);
    const int wristCount = int(leftWrist) + int(rightWrist);
    // Wrist movement must be measured relative to the person. Otherwise a
    // camera pan or whole-body translation can be labelled as raising or
    // waving a hand even when the pose has not changed.
    const double leftWristY = leftWrist ? keypointDelta(first, last, 9, true, scale) - dy : 0.0;
    const double rightWristY = rightWrist ? keypointDelta(first, last, 10, true, scale) - dy : 0.0;
    const double leftLift = -leftWristY, rightLift = -rightWristY;
    // A stationary second hand must not halve a real one-handed gesture.
    // Opposing clear movements are a mixed action rather than an arbitrary
    // choice of one hand.
    const bool opposingLift = wristCount == 2
        && ((leftLift > .10 && rightLift < -.10) || (rightLift > .10 && leftLift < -.10));
    const double wristLift = wristCount == 2
        ? (std::abs(leftLift) >= std::abs(rightLift) ? leftLift : rightLift)
        : leftWrist ? leftLift : rightWrist ? rightLift : 0.0;
    const double wristSpread = (leftWrist ? std::abs(keypointDelta(first, last, 9, false, scale) - dx) : 0.0)
        + (rightWrist ? std::abs(keypointDelta(first, last, 10, false, scale) - dx) : 0.0);
    const bool shoulders = observedAtBothEnds(first, last, 5) && observedAtBothEnds(first, last, 6);
    const double shoulderTurn = shoulders ? std::abs(keypointDelta(first, last, 5, false, scale)
                                         - keypointDelta(first, last, 6, false, scale)) : 0.0;
    if (opposingLift) {
        result.gesture = GestureClass::Mixed;
        result.gestureConfidence = std::clamp(std::min(std::abs(leftLift),std::abs(rightLift)) / 0.30,0.0,1.0);
    } else if (wristCount && wristLift > 0.10) {
        result.gesture = GestureClass::Raise;
        result.gestureConfidence = std::clamp(wristLift / 0.30, 0.0, 1.0);
    } else if (wristCount && wristLift < -0.10) {
        result.gesture = GestureClass::Lower;
        result.gestureConfidence = std::clamp(-wristLift / 0.30, 0.0, 1.0);
    } else if (wristCount && wristSpread > 0.22 && displacement < 0.18) {
        result.gesture = GestureClass::Wave;
        result.gestureConfidence = std::clamp(wristSpread / 0.6, 0.0, 1.0);
    } else if (shoulderTurn > 0.20) {
        result.gesture = GestureClass::Turn;
        result.gestureConfidence = std::clamp(shoulderTurn / 0.6, 0.0, 1.0);
    } else if (displacement > 0.10
               && (observedAtBothEnds(first,last,15) || observedAtBothEnds(first,last,16))) {
        result.gesture = GestureClass::Step;
        result.gestureConfidence = std::clamp(displacement / 0.35, 0.0, 1.0);
    } else {
        result.gesture = wristCount == 2 && displacement < 0.035 ? GestureClass::Static : GestureClass::Unknown;
        result.gestureConfidence = result.gesture == GestureClass::Static ? 1.0 : 0.0;
    }
    return result;
}

MovementClassification classifyOne(const MotionWindow& window)
{
    if (window.frames.size() < 2) return {};
    auto result = classifyEndpoints(window.frames.front(), window.frames.back());
    const bool checkDirection = result.direction == MovementDirection::Static;
    const bool checkGesture = result.gesture == GestureClass::Static;
    if (!checkDirection && !checkGesture)
        return result;

    // Equal endpoints do not imply a static trajectory (raise/lower, nod,
    // out-and-back). Require a sustained excursion rather than one outlier.
    // The temporal matcher remains responsible for matching both trajectories.
    std::size_t directionConsecutive = 0, gestureConsecutive = 0;
    for (std::size_t i = 1; i + 1 < window.frames.size(); ++i) {
        const auto excursion = classifyEndpoints(window.frames.front(), window.frames[i]);
        const bool directionMoving = excursion.direction != MovementDirection::Static
            && excursion.direction != MovementDirection::Unknown;
        const bool gestureMoving = excursion.gesture != GestureClass::Static
            && excursion.gesture != GestureClass::Unknown;
        directionConsecutive = directionMoving ? directionConsecutive + 1 : 0;
        gestureConsecutive = gestureMoving ? gestureConsecutive + 1 : 0;
        if (checkDirection && directionConsecutive >= 2) {
            result.direction = MovementDirection::Mixed;
            result.directionConfidence = excursion.directionConfidence;
        }
        if (checkGesture && gestureConsecutive >= 2) {
            result.gesture = GestureClass::Mixed;
            result.gestureConfidence = excursion.gestureConfidence;
        }
        if ((!checkDirection || result.direction != MovementDirection::Static)
            && (!checkGesture || result.gesture != GestureClass::Static)) break;
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

MovementClassification MovementClassifier::classify(const MotionWindow& window, double start, double end) const
{
    MotionWindow supported;
    for (const auto& frame : window.frames)
        // Matched video ranges are half-open; the frame at end belongs to the
        // next clip and must not supply its direction or gesture evidence.
        if (frame.timestampSeconds >= start - 1e-6 && frame.timestampSeconds < end - 1e-6)
            supported.frames.push_back(frame);
    return classifyOne(supported);
}

MovementClassification MovementClassifier::classify(const MotionWindow& left,
                                                    const MotionWindow& right) const
{
    const auto a = classifyOne(left);
    const auto b = classifyOne(right);
    return combineClassifications(a, b);
}

MovementClassification MovementClassifier::combineClassifications(
    const MovementClassification& a, const MovementClassification& b)
{
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
    case GestureClass::RaiseLower: return "raise_lower";
    case GestureClass::ReachReturn: return "reach_return";
    case GestureClass::HeadTurn: return "head_turn";
    case GestureClass::HeadNod: return "head_nod";
    case GestureClass::HeadTilt: return "head_tilt";
    case GestureClass::Unknown: break;
    }
    return "unknown";
}

} // namespace pfcore
