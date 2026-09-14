#include <pfcore/DominantPerson.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace pfcore {

double BoundingBox::area() const noexcept
{
    return std::max(0.0, right - left) * std::max(0.0, bottom - top);
}

double BoundingBox::iou(const BoundingBox& other) const noexcept
{
    const double leftEdge = std::max(left, other.left);
    const double topEdge = std::max(top, other.top);
    const double rightEdge = std::min(right, other.right);
    const double bottomEdge = std::min(bottom, other.bottom);
    const double intersection = std::max(0.0, rightEdge - leftEdge)
        * std::max(0.0, bottomEdge - topEdge);
    const double unionArea = area() + other.area() - intersection;
    return unionArea <= 1e-12 ? 0.0 : intersection / unionArea;
}

namespace {

double centerDistanceScore(const BoundingBox& left, const BoundingBox& right) noexcept
{
    const double leftWidth = std::max(1e-9, left.right - left.left);
    const double leftHeight = std::max(1e-9, left.bottom - left.top);
    const double rightWidth = std::max(1e-9, right.right - right.left);
    const double rightHeight = std::max(1e-9, right.bottom - right.top);
    const double leftCenterX = (left.left + left.right) * 0.5;
    const double leftCenterY = (left.top + left.bottom) * 0.5;
    const double rightCenterX = (right.left + right.right) * 0.5;
    const double rightCenterY = (right.top + right.bottom) * 0.5;
    const double scale = std::max(1e-9, std::sqrt(leftWidth * leftHeight)
        + std::sqrt(rightWidth * rightHeight)) * 0.5;
    const double distance = std::hypot(leftCenterX - rightCenterX,
                                       leftCenterY - rightCenterY) / scale;
    return std::exp(-2.5 * distance);
}

double keypointScore(const PersonDetection& left, const PersonDetection& right) noexcept
{
    const std::size_t count = std::min(left.keypoints.size(), right.keypoints.size());
    if (count == 0) return 0.5;
    const double scale = std::max(1e-9, std::sqrt(left.box.area())
        + std::sqrt(right.box.area())) * 0.5;
    double weightedDistance = 0.0;
    double weight = 0.0;
    for (std::size_t index = 0; index < count; ++index) {
        const auto& a = left.keypoints[index];
        const auto& b = right.keypoints[index];
        const double confidence = std::clamp(std::min(a.confidence, b.confidence), 0.0, 1.0);
        if (confidence <= 1e-6) continue;
        weightedDistance += confidence * std::hypot(a.x - b.x, a.y - b.y) / scale;
        weight += confidence;
    }
    if (weight <= 1e-9) return 0.5;
    return std::exp(-4.0 * (weightedDistance / weight));
}

} // namespace

double PersonTrack::totalTimeSeconds() const noexcept
{
    double total = 0.0;
    for (const auto& observation : observations) {
        if (observation.frameDurationSeconds > 0.0) total += observation.frameDurationSeconds;
    }
    if (total > 0.0) return total;
    if (observations.size() < 2) return 0.0;
    return std::max(0.0, observations.back().timestampSeconds - observations.front().timestampSeconds);
}

double PersonTrack::averageArea() const noexcept
{
    if (observations.empty()) return 0.0;
    double sum = 0.0;
    for (const auto& observation : observations) sum += observation.box.area();
    return sum / static_cast<double>(observations.size());
}

double PersonTrack::averageKeypointConfidence() const noexcept
{
    if (observations.empty()) return 0.0;
    double sum = 0.0;
    for (const auto& observation : observations) sum += observation.keypointConfidence;
    return sum / static_cast<double>(observations.size());
}

double PersonTrack::firstTimestampSeconds() const noexcept
{
    return observations.empty() ? 0.0 : observations.front().timestampSeconds;
}

DominantPersonTracker::DominantPersonTracker(double iouThreshold, double maxGapSeconds)
    : iouThreshold_(iouThreshold)
    , maxGapSeconds_(maxGapSeconds)
{
    if (!(iouThreshold_ >= 0.0 && iouThreshold_ <= 1.0)) {
        throw std::invalid_argument("DominantPersonTracker: IoU threshold must be in [0, 1]");
    }
    if (!(maxGapSeconds_ > 0.0)) {
        throw std::invalid_argument("DominantPersonTracker: max gap must be > 0");
    }
}

void DominantPersonTracker::reset()
{
    tracks_.clear();
    nextId_ = 1;
}

void DominantPersonTracker::update(double timestampSeconds,
                                   double frameDurationSeconds,
                                   const std::vector<PersonDetection>& detections)
{
    std::vector<bool> trackUsed(tracks_.size(), false);
    std::vector<bool> detectionUsed(detections.size(), false);

    struct Candidate {
        double score;
        std::size_t track;
        std::size_t detection;
    };
    std::vector<Candidate> candidates;
    for (std::size_t track = 0; track < tracks_.size(); ++track) {
        if (tracks_[track].observations.empty()) continue;
        for (std::size_t detection = 0; detection < detections.size(); ++detection) {
            const auto& last = tracks_[track].observations.back();
            if (timestampSeconds < last.timestampSeconds
                || timestampSeconds - last.timestampSeconds > maxGapSeconds_) continue;
            const double iou = last.box.iou(detections[detection].box);
            const double center = centerDistanceScore(last.box, detections[detection].box);
            const double keypoints = keypointScore(last, detections[detection]);
            // IoU is still the strongest signal, but center/keypoint continuity
            // keeps an ID stable when a person turns or the detector jitters.
            // The gate prevents a stale track from stealing a new person merely
            // because their boxes overlap for one frame.
            const bool gated = iou >= iouThreshold_
                || (center >= 0.42 && keypoints >= 0.38);
            if (!gated) continue;
            const double score = 0.55 * iou + 0.25 * center + 0.20 * keypoints;
            candidates.push_back({score, track, detection});
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& left, const Candidate& right) {
        if (std::abs(left.score - right.score) > 1e-12) return left.score > right.score;
        if (left.track != right.track) return left.track < right.track;
        return left.detection < right.detection;
    });
    for (const Candidate& candidate : candidates) {
        if (trackUsed[candidate.track] || detectionUsed[candidate.detection]) continue;
        trackUsed[candidate.track] = true;
        detectionUsed[candidate.detection] = true;
        PersonDetection observation = detections[candidate.detection];
        observation.timestampSeconds = timestampSeconds;
        if (observation.frameDurationSeconds <= 0.0) observation.frameDurationSeconds = frameDurationSeconds;
        tracks_[candidate.track].observations.push_back(std::move(observation));
    }

    for (std::size_t detection = 0; detection < detections.size(); ++detection) {
        if (detectionUsed[detection]) continue;
        PersonDetection observation = detections[detection];
        observation.timestampSeconds = timestampSeconds;
        if (observation.frameDurationSeconds <= 0.0) observation.frameDurationSeconds = frameDurationSeconds;
        PersonTrack track;
        track.id = nextId_++;
        track.observations.push_back(std::move(observation));
        tracks_.push_back(std::move(track));
    }
}

std::optional<PersonTrack> DominantPersonTracker::dominant() const
{
    if (tracks_.empty()) return std::nullopt;
    const auto best = std::max_element(tracks_.begin(), tracks_.end(), [](const PersonTrack& left,
                                                                          const PersonTrack& right) {
        constexpr double epsilon = 1e-9;
        if (std::abs(left.totalTimeSeconds() - right.totalTimeSeconds()) > epsilon)
            return left.totalTimeSeconds() < right.totalTimeSeconds();
        if (std::abs(left.averageArea() - right.averageArea()) > epsilon)
            return left.averageArea() < right.averageArea();
        if (std::abs(left.averageKeypointConfidence() - right.averageKeypointConfidence()) > epsilon)
            return left.averageKeypointConfidence() < right.averageKeypointConfidence();
        if (std::abs(left.firstTimestampSeconds() - right.firstTimestampSeconds()) > epsilon)
            return left.firstTimestampSeconds() > right.firstTimestampSeconds();
        return left.id > right.id;
    });
    return *best;
}

} // namespace pfcore
