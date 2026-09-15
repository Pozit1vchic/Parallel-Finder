#include "pfcore/MotionRanker.hpp"

#include <algorithm>
#include <cmath>

#include "pfcore/MovementClassifier.hpp"

namespace pfcore {

void MotionRanker::rank(std::vector<MotionMatch>& matches,
                        const std::vector<MotionWindow>& windows)
{
    MovementClassifier classifier;
    for (auto& match : matches) {
        if (match.leftIndex >= windows.size() || match.rightIndex >= windows.size()) {
            match.rankScore = match.similarity;
            continue;
        }
        const auto classification = classifier.classify(windows[match.leftIndex],
                                                         windows[match.rightIndex]);
        match.directionLabel = MovementClassifier::directionName(classification.direction);
        match.gestureLabel = MovementClassifier::gestureName(classification.gesture);
        const double durationFactor = std::clamp(match.durationSeconds / 1.0, 0.0, 1.0);
        const double confidenceFactor = 0.8 + 0.2 * std::min(classification.directionConfidence,
                                                               classification.gestureConfidence);
        match.rankScore = std::clamp(match.similarity
                                     * (0.75 + 0.25 * durationFactor)
                                     * confidenceFactor, 0.0, 1.0);
    }
    // Final semantic guard: pose jitter can occasionally survive numeric
    // prefilters, but a pair classified static in both axes is not a motion
    // result. Keep a static camera with a real hand gesture (gesture != static)
    // eligible; remove only the exact static/static false-positive class.
    matches.erase(std::remove_if(matches.begin(), matches.end(), [&](const auto& match) {
        const bool explicitStaticAnalysis = match.leftIndex < windows.size()
            && match.rightIndex < windows.size()
            && windows[match.leftIndex].staticFrameSet
            && windows[match.rightIndex].staticFrameSet;
        return !explicitStaticAnalysis
            && match.directionLabel == "static" && match.gestureLabel == "static";
    }), matches.end());
    std::stable_sort(matches.begin(), matches.end(), [](const MotionMatch& left,
                                                        const MotionMatch& right) {
        // The user-facing order is the calibrated similarity percentage. The
        // classifier confidence remains a tie-breaker, never a way to move a
        // weaker match above a stronger one.
        if (std::abs(left.similarity - right.similarity) > 1e-12)
            return left.similarity > right.similarity;
        if (std::abs(left.rankScore - right.rankScore) > 1e-12)
            return left.rankScore > right.rankScore;
        if (left.leftIndex != right.leftIndex) return left.leftIndex < right.leftIndex;
        return left.rightIndex < right.rightIndex;
    });
}

} // namespace pfcore
