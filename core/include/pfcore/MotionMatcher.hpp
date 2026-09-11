#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace pfcore {

struct Keypoint {
    double x = 0.0;
    double y = 0.0;
    double confidence = 1.0;
};

struct PoseFrame {
    double timestampSeconds = 0.0;
    std::vector<Keypoint> keypoints;
};

struct MotionWindow {
    std::string sourceId;
    std::vector<PoseFrame> frames;
};

struct MotionMatcherParams {
    double similarityThreshold = 0.72;
    double candidateThreshold = 0.45;
    double minRepeatGapSeconds = 0.0;
    double sameVideoGapSeconds = 0.5;
    double crossVideoGapSeconds = 0.0;
    double duplicateWindowSeconds = 1.0;
    double noiseCoefficient = 0.15;
    std::size_t maxUnique = 100;
    double timeWeight = 0.10;
    std::size_t dtwBand = 8;
};

struct MotionMatch {
    std::size_t leftIndex = 0;
    std::size_t rightIndex = 0;
    double similarity = 0.0;
    double dtwDistance = 0.0;
    double durationSeconds = 0.0;
};

class MotionMatcher {
public:
    explicit MotionMatcher(MotionMatcherParams params = {});

    const MotionMatcherParams& params() const noexcept { return params_; }
    void setParams(MotionMatcherParams params);

    // Compares two windows using normalized pose trajectories and constrained
    // DTW. Returns a value in [0, 1], where 1 means identical motion.
    MotionMatch compare(const MotionWindow& left, const MotionWindow& right,
                        std::size_t leftIndex = 0, std::size_t rightIndex = 0) const;

    // All-pairs search. Windows are never consumed: one window may participate
    // in several independent results, as required by the product semantics.
    std::vector<MotionMatch> findAllPairs(const std::vector<MotionWindow>& windows) const;

private:
    MotionMatcherParams params_;
};

} // namespace pfcore
