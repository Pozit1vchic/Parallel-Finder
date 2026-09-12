#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "pfcore/MotionMatcher.hpp"

namespace pfcore {

struct BoundingBox {
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;

    double area() const noexcept;
    double iou(const BoundingBox& other) const noexcept;
};

struct PersonDetection {
    double timestampSeconds = 0.0;
    double frameDurationSeconds = 0.0;
    BoundingBox box;
    double confidence = 0.0;
    double keypointConfidence = 0.0;
    std::vector<Keypoint> keypoints;
};

struct PersonTrack {
    std::size_t id = 0;
    std::vector<PersonDetection> observations;

    double totalTimeSeconds() const noexcept;
    double averageArea() const noexcept;
    double averageKeypointConfidence() const noexcept;
    double firstTimestampSeconds() const noexcept;
};

class DominantPersonTracker {
public:
    explicit DominantPersonTracker(double iouThreshold = 0.30,
                                   double maxGapSeconds = 1.0);

    void reset();
    void update(double timestampSeconds,
                double frameDurationSeconds,
                const std::vector<PersonDetection>& detections);
    const std::vector<PersonTrack>& tracks() const noexcept { return tracks_; }
    std::optional<PersonTrack> dominant() const;

private:
    double iouThreshold_;
    double maxGapSeconds_;
    std::size_t nextId_ = 1;
    std::vector<PersonTrack> tracks_;
};

} // namespace pfcore
