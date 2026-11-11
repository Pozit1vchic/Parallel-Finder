#include "pfcore/MotionRanker.hpp"

#include <algorithm>
#include <cmath>

#include "pfcore/MovementClassifier.hpp"
#include "pfcore/detail/MatchOrdering.hpp"

namespace pfcore {

void MotionRanker::rank(std::vector<MotionMatch>& matches,
                        const std::vector<MotionWindow>& windows)
{
    MovementClassifier classifier;
    const auto classifyInterval=[&](const MotionWindow& window,double start,double end) {
        if(std::isfinite(start) && std::isfinite(end) && end>start)
            return classifier.classify(window,start,end);
        // Older callers do not supply matched boundaries. Preserve their
        // whole-window behavior, but do not guess for malformed explicit ranges.
        if(start==0. && end==0.)return classifier.classify(window);
        return MovementClassification{};
    };
    for (auto& match : matches) {
        if (match.leftIndex >= windows.size() || match.rightIndex >= windows.size()) {
            match.rankScore = match.similarity;
            continue;
        }
        if (windows[match.leftIndex].staticFrameSet && windows[match.rightIndex].staticFrameSet) {
            match.directionLabel = "static";
            match.gestureLabel = "static";
            match.rankScore = match.similarity;
            continue;
        }
        const auto classification = MovementClassifier::combineClassifications(
            classifyInterval(windows[match.leftIndex],match.leftStartSeconds,match.leftEndSeconds),
            classifyInterval(windows[match.rightIndex],match.rightStartSeconds,match.rightEndSeconds));
        // A combined label may be inherited from only one side. Zero
        // confidence means the other side could not support the claim, so
        // avoid presenting that label as a measured property of the pair.
        match.directionLabel = classification.directionConfidence > 0.0
            ? MovementClassifier::directionName(classification.direction) : "unknown";
        match.gestureLabel = classification.gestureConfidence > 0.0
            ? MovementClassifier::gestureName(classification.gesture) : "unknown";
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
        if (explicitStaticAnalysis || match.directionLabel != "static" || match.gestureLabel != "static")
            return false;
        if (match.leftIndex >= windows.size() || match.rightIndex >= windows.size()) return false;
        // Labels above are static only when both sides support them with
        // positive confidence. Reuse that decision rather than classifying
        // the complete windows again and including an unrelated action.
        return true;
    }), matches.end());
    std::stable_sort(matches.begin(), matches.end(), [](const MotionMatch& left,
                                                        const MotionMatch& right) {
        // The user-facing order is the similarity score. The
        // classifier confidence remains a tie-breaker, never a way to move a
        // weaker match above a stronger one.
        const auto similarityOrder=detail::descendingScoreOrder(left.similarity,right.similarity);
        if (similarityOrder != 0) return similarityOrder < 0;
        const auto rankOrder=detail::descendingScoreOrder(left.rankScore,right.rankScore);
        if (rankOrder != 0) return rankOrder < 0;
        if (left.leftIndex != right.leftIndex) return left.leftIndex < right.leftIndex;
        return left.rightIndex < right.rightIndex;
    });
}

} // namespace pfcore
