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

double embeddingScore(const std::vector<float>& left, const std::vector<float>& right) noexcept
{
    if (left.empty() || right.empty()
        || left.size() != right.size()) {
        return -1.0;
    }
    double dot = 0.0;
    double leftNorm = 0.0;
    double rightNorm = 0.0;
    for (std::size_t index = 0; index < left.size(); ++index) {
        const float a = left[index];
        const float b = right[index];
        if (!std::isfinite(a) || !std::isfinite(b)) return -1.0;
        dot += static_cast<double>(a) * b;
        leftNorm += static_cast<double>(a) * a;
        rightNorm += static_cast<double>(b) * b;
    }
    if (leftNorm <= 1e-12 || rightNorm <= 1e-12) return -1.0;
    return std::clamp(dot / std::sqrt(leftNorm * rightNorm), -1.0, 1.0);
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
            // ReID is sampled every few frames for throughput. Use the latest
            // available embedding in the track instead of treating an
            // unsampled frame as an identity-free observation.
            const PersonDetection* appearanceReference = &last;
            for (auto observation = tracks_[track].observations.rbegin();
                 observation != tracks_[track].observations.rend(); ++observation) {
                if (!observation->appearanceEmbedding.empty()) {
                    appearanceReference = &*observation;
                    break;
                }
            }
            const double appearance = embeddingScore(appearanceReference->appearanceEmbedding,
                                                       detections[detection].appearanceEmbedding);
            double face = -1.0;
            if (!detections[detection].faceEmbedding.empty()) {
                for (auto observation = tracks_[track].observations.rbegin();
                     observation != tracks_[track].observations.rend(); ++observation) {
                    if (observation->faceEmbedding.empty()) continue;
                    face = embeddingScore(observation->faceEmbedding, detections[detection].faceEmbedding);
                    break;
                }
            }
            // IoU is still the strongest signal, but center/keypoint continuity
            // keeps an ID stable when a person turns or the detector jitters.
            // The gate prevents a stale track from stealing a new person merely
            // because their boxes overlap for one frame.
            // Once both observations have a body-ReID embedding, a low
            // identity similarity must veto the geometric match. This keeps
            // two people from swapping track IDs when they cross or stand in
            // the same shot.
            if (face > -1.0 && face < 0.363) continue;
            if (face <= -1.0 && appearance >= 0.0 && appearance < 0.45) continue;
            const bool gated = iou >= iouThreshold_
                || (center >= 0.42 && keypoints >= 0.38);
            if (!gated) continue;
            const double score = 0.48 * iou + 0.22 * center + 0.18 * keypoints
                + (appearance >= 0.0 ? 0.12 * std::max(0.0, appearance) : 0.0);
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

std::vector<bool> selectDominantIdentities(const std::vector<IdentitySummary>& identities)
{
    const auto count = identities.size();
    std::vector<bool> selected(count, false);
    if (!count) return selected;
    const auto faceReady = [&](std::size_t i) {
        return identities[i].faceEvidence >= 0.45
            && embeddingScore(identities[i].face, identities[i].face) > 0.99;
    };
    const auto bodyReady = [&](std::size_t i) {
        return identities[i].bodyEvidence >= 0.45
            && embeddingScore(identities[i].body, identities[i].body) > 0.99;
    };
    struct Edge { std::size_t a, b; double score; bool face; };
    std::vector<Edge> edges;
    for (std::size_t a = 0; a < count; ++a) for (std::size_t b = a + 1; b < count; ++b) {
        const bool face = faceReady(a) && faceReady(b);
        if (!face && !(bodyReady(a) && bodyReady(b))) continue;
        const double score = face ? embeddingScore(identities[a].face, identities[b].face)
                                  : embeddingScore(identities[a].body, identities[b].body);
        if (score >= (face ? 0.363 : 0.76)) edges.push_back({a, b, score, face});
    }
    // Build reliable face groups before allowing weaker clothing links.
    std::stable_sort(edges.begin(), edges.end(), [](const Edge& a, const Edge& b) {
        if (a.face != b.face) return a.face > b.face;
        if (a.score != b.score) return a.score > b.score;
        if (a.a != b.a) return a.a < b.a;
        return a.b < b.b;
    });
    std::vector<std::size_t> owner(count);
    std::vector<std::vector<std::size_t>> members(count);
    for (std::size_t i = 0; i < count; ++i) { owner[i] = i; members[i].push_back(i); }
    for (const auto& edge : edges) {
        const auto a = owner[edge.a], b = owner[edge.b];
        if (a == b) continue;
        bool conflict = false;
        for (const auto left : members[a]) {
            if (!faceReady(left)) continue;
            for (const auto right : members[b]) {
                if (faceReady(right)
                    && embeddingScore(identities[left].face, identities[right].face) < 0.363) {
                    conflict = true; break;
                }
            }
            if (conflict) break;
        }
        if (conflict) continue;
        for (const auto i : members[b]) { owner[i] = a; members[a].push_back(i); }
        members[b].clear();
    }
    std::size_t best = 0;
    double bestDuration = -1, bestArea = -1;
    bool bestEvidence = false;
    for (std::size_t group = 0; group < count; ++group) {
        if (members[group].empty()) continue;
        double duration = 0, area = 0;
        bool evidence = false;
        for (const auto i : members[group]) {
            duration += std::max(0.0, identities[i].duration);
            area += std::max(0.0, identities[i].area);
            evidence = evidence || faceReady(i) || bodyReady(i);
        }
        if ((evidence && !bestEvidence)
            || (evidence == bestEvidence && (duration > bestDuration + 1e-9
                || (std::abs(duration - bestDuration) <= 1e-9 && area > bestArea)))) {
            best = group; bestDuration = duration; bestArea = area; bestEvidence = evidence;
        }
    }
    for (const auto i : members[best]) selected[i] = true;
    return selected;
}

} // namespace pfcore
