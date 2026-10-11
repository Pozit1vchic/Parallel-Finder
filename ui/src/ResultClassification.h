#pragma once

#include <pfcore/MovementClassifier.hpp>
#include <QVariantMap>
#include <array>

namespace pfui {
using ResultPoseEvidence = std::array<pfcore::MotionWindow, 2>;

// Keep measured poses for range edits without retaining image descriptors,
// identity prototypes, or a second copy of the analysis cache.
inline pfcore::MotionWindow groupingEvidence(const pfcore::MotionWindow& source) {
    pfcore::MotionWindow result;
    result.frames = source.frames;
    result.sceneStartSeconds = source.sceneStartSeconds;
    result.sceneEndSeconds = source.sceneEndSeconds;
    result.staticFrameSet = source.staticFrameSet;
    return result;
}

inline void classifyResultRange(QVariantMap& row, pfcore::MotionMatch& match,
                                const ResultPoseEvidence& evidence) {
    const pfcore::MovementClassifier classifier;
    const auto a = classifier.classifyForGrouping(evidence[0], match.leftStartSeconds,
                                                  match.leftEndSeconds, match.headOnlyComparison);
    const auto b = classifier.classifyForGrouping(evidence[1], match.rightStartSeconds,
                                                  match.rightEndSeconds, match.headOnlyComparison);
    const auto movement = pfcore::MovementClassifier::combineForGrouping(a,b);
    match.directionLabel = pfcore::MovementClassifier::directionName(movement.direction);
    match.gestureLabel = pfcore::MovementClassifier::gestureName(movement.gesture);
    row.insert("direction", QString::fromStdString(match.directionLabel));
    row.insert("gesture", QString::fromStdString(match.gestureLabel));
    if (row.value("classificationManual").toBool()) return;
    QString classification;
    if (evidence[0].staticFrameSet && evidence[1].staticFrameSet) {
        const auto left = classifier.classifyPoseForGrouping(evidence[0], match.leftStartSeconds,
                                                              match.leftEndSeconds, match.headOnlyComparison);
        const auto right = classifier.classifyPoseForGrouping(evidence[1], match.rightStartSeconds,
                                                               match.rightEndSeconds, match.headOnlyComparison);
        const std::string fallback = match.headOnlyComparison ? "head_pose" : "body_pose";
        classification = QString::fromStdString(left == right ? left
            : (left == fallback || right == fallback ? fallback : std::string("mixed")));
    } else {
        classification = QString::fromStdString(match.gestureLabel + "/" + match.directionLabel);
    }
    row.insert("classification", classification);
}
} // namespace pfui
