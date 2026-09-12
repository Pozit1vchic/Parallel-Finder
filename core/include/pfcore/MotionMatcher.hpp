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
    double similarityThreshold = 0.85;
    double candidateThreshold = 0.55;
    double minRepeatGapSec = 6.0;
    double sameFileGapSec = 2.0;
    double crossFileGapSec = 0.0;
    double duplicateWindowSec = 1.5;
    double noiseFactor = 1.0;
    std::size_t maxUniqueResults = 100;
    double timeWeight = 0.25;
    std::size_t dtwBand = 8;
};

struct MotionMatch {
    std::size_t leftIndex = 0;
    std::size_t rightIndex = 0;
    std::string leftSourceId;
    std::string rightSourceId;
    double similarity = 0.0;
    double dtwDistance = 0.0;
    double durationSeconds = 0.0;
    double leftStartSeconds = 0.0;
    double leftEndSeconds = 0.0;
    double rightStartSeconds = 0.0;
    double rightEndSeconds = 0.0;
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
