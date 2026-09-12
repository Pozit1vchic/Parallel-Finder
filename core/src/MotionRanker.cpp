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
    std::stable_sort(matches.begin(), matches.end(), [](const MotionMatch& left,
                                                        const MotionMatch& right) {
        if (std::abs(left.rankScore - right.rankScore) > 1e-12)
            return left.rankScore > right.rankScore;
        if (std::abs(left.similarity - right.similarity) > 1e-12)
            return left.similarity > right.similarity;
        if (left.leftIndex != right.leftIndex) return left.leftIndex < right.leftIndex;
        return left.rightIndex < right.rightIndex;
    });
}

} // namespace pfcore
