#include <pfcore/DominantPerson.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>
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
    sceneTrackStart_ = 0;
}

void DominantPersonTracker::retrackScenes(const std::vector<double>& boundaries)
{
    // Validate before moving observations: a malformed boundary list must not
    // partially destroy the existing tracking result.
    if (!std::is_sorted(boundaries.begin(), boundaries.end())
        || !std::all_of(boundaries.begin(), boundaries.end(),
                        [](double time) { return std::isfinite(time); }))
        throw std::invalid_argument("PersonTracker: scene boundaries must be finite and ordered");
    if (boundaries.empty() || tracks_.empty()) return;
    std::size_t count = 0;
    for (const auto& track : tracks_) count += track.observations.size();
    std::vector<PersonDetection> observations;
    observations.reserve(count);
    for (auto& track : tracks_)
        for (auto& observation : track.observations)
            observations.push_back(std::move(observation));
    std::stable_sort(observations.begin(), observations.end(), [](const auto& a, const auto& b) {
        return a.timestampSeconds < b.timestampSeconds;
    });
    reset();
    std::size_t boundary = 0;
    for (std::size_t first = 0; first < observations.size();) {
        const double timestamp = observations[first].timestampSeconds;
        while (boundary < boundaries.size() && boundaries[boundary] <= timestamp) {
            sceneTrackStart_ = tracks_.size();
            ++boundary;
        }
        std::size_t last = first + 1;
        while (last < observations.size() && observations[last].timestampSeconds == timestamp) ++last;
        std::vector<PersonDetection> frame;
        frame.reserve(last - first);
        for (auto i = first; i < last; ++i) frame.push_back(std::move(observations[i]));
        update(timestamp, 0.0, frame);
        first = last;
    }
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
    const bool needsBody = std::any_of(detections.begin(), detections.end(),
        [](const auto& detection) { return !detection.appearanceEmbedding.empty(); });
    const bool needsFace = std::any_of(detections.begin(), detections.end(),
        [](const auto& detection) { return !detection.faceEmbedding.empty(); });
    for (std::size_t track = sceneTrackStart_; track < tracks_.size(); ++track) {
        if (tracks_[track].observations.empty()) continue;
        const auto& last = tracks_[track].observations.back();
        if (timestampSeconds < last.timestampSeconds
            || timestampSeconds - last.timestampSeconds > maxGapSeconds_) continue;
        const PersonDetection* appearanceReference = nullptr;
        const PersonDetection* faceReference = nullptr;
        for (auto observation = tracks_[track].observations.rbegin();
             observation != tracks_[track].observations.rend()
                && ((needsBody && !appearanceReference) || (needsFace && !faceReference)); ++observation) {
            if (needsBody && !appearanceReference && !observation->appearanceEmbedding.empty())
                appearanceReference = &*observation;
            if (needsFace && !faceReference && !observation->faceEmbedding.empty()) faceReference = &*observation;
        }
        for (std::size_t detection = 0; detection < detections.size(); ++detection) {
            const double iou = last.box.iou(detections[detection].box);
            const double center = centerDistanceScore(last.box, detections[detection].box);
            const double keypoints = keypointScore(last, detections[detection]);
            // ReID is sampled every few frames for throughput. Use the latest
            // available embedding in the track instead of treating an
            // unsampled frame as an identity-free observation.
            const double appearance = appearanceReference
                ? embeddingScore(appearanceReference->appearanceEmbedding,
                                 detections[detection].appearanceEmbedding) : -1.0;
            const double face = faceReference
                ? embeddingScore(faceReference->faceEmbedding, detections[detection].faceEmbedding) : -1.0;
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
    const auto faceObserved = [&](std::size_t i) {
        return identities[i].faceEvidence > 0.0
            && embeddingScore(identities[i].face, identities[i].face) > 0.99;
    };
    const auto faceReady = [&](std::size_t i) {
        return identities[i].faceEvidence >= 0.45 && faceObserved(i);
    };
    const auto bodyReady = [&](std::size_t i) {
        return identities[i].bodyEvidence >= 0.45
            && embeddingScore(identities[i].body, identities[i].body) > 0.99;
    };
    struct Edge { std::size_t a, b; double score; bool face; };
    const auto coVisible = [&](std::size_t a, std::size_t b) {
        const auto& left = identities[a].observationTimes;
        const auto& right = identities[b].observationTimes;
        if (left.empty() || right.empty() || left.back() < right.front()
            || right.back() < left.front()) return false;
        std::size_t i = 0, j = 0;
        while (i < left.size() && j < right.size()) {
            if (std::abs(left[i] - right[j]) <= 1e-6) return true;
            if (left[i] < right[j]) ++i; else ++j;
        }
        return false;
    };
    std::vector<Edge> edges;
    for (std::size_t a = 0; a < count; ++a) for (std::size_t b = a + 1; b < count; ++b) {
        if (coVisible(a,b)) continue;
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
            for (const auto right : members[b]) {
                // A single sampled face cannot establish a positive identity
                // link, but contradictory facial evidence must still veto a
                // clothing bridge. Otherwise a short extra joins the lead
                // simply because its face has not reached two samples yet.
                if (coVisible(left,right) || (faceObserved(left) && faceObserved(right)
                    && embeddingScore(identities[left].face, identities[right].face) < 0.363)) {
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
            area += std::max(0.0, identities[i].area) * std::max(0.0, identities[i].duration);
            evidence = evidence || faceReady(i) || bodyReady(i);
        }
        // Embedding availability verifies identity links, not narrative
        // importance. A brief front-facing extra must not defeat a lead seen
        // for much longer in profile. Area is duration-weighted so splitting
        // a track into fragments cannot inflate its tie-breaking prominence.
        if (duration > bestDuration + 1e-9
            || (std::abs(duration - bestDuration) <= 1e-9
                && (area > bestArea + 1e-9
                    || (std::abs(area - bestArea) <= 1e-9 && evidence && !bestEvidence)))) {
            best = group; bestDuration = duration; bestArea = area; bestEvidence = evidence;
        }
    }
    for (const auto i : members[best]) selected[i] = true;
    return selected;
}

std::vector<bool> selectDominantSceneTracks(const std::vector<PersonTrack>& tracks,
                                          double startSeconds, double endSeconds,
                                          const std::vector<bool>& preferredTracks)
{
    std::vector<bool> selected(tracks.size(), false);
    if (std::isnan(startSeconds) || std::isnan(endSeconds)
        || endSeconds <= startSeconds) return selected;
    // Preserve a verified lead without recomputing every embedding in the
    // shot. Binary searches also avoid scanning long tracks per scene.
    for (std::size_t index = 0; index < std::min(tracks.size(), preferredTracks.size()); ++index) {
        if (!preferredTracks[index]) continue;
        const auto& observations = tracks[index].observations;
        const auto first = std::lower_bound(observations.begin(), observations.end(), startSeconds,
            [](const auto& observation, double time) { return observation.timestampSeconds < time; });
        selected[index] = first != observations.end() && std::next(first) != observations.end()
            && std::next(first)->timestampSeconds < endSeconds;
    }
    // A supplied file-wide identity is a restriction, not a preference that
    // can be replaced when the lead is absent (or has too few observations).
    // Two shots of an extra can pass pairwise same-person verification; that
    // does not make the extra the selected lead of this source.
    if (!preferredTracks.empty()) return selected;
    std::vector<IdentitySummary> summaries;
    std::vector<std::size_t> indices;
    for (std::size_t index = 0; index < tracks.size(); ++index) {
        const auto& observations = tracks[index].observations;
        const auto first = std::lower_bound(observations.begin(), observations.end(), startSeconds,
            [](const auto& observation, double time) { return observation.timestampSeconds < time; });
        const auto last = std::lower_bound(first, observations.end(), endSeconds,
            [](const auto& observation, double time) { return observation.timestampSeconds < time; });
        const auto count = std::distance(first, last);
        if (count < 2) continue;
        IdentitySummary summary;
        summary.observationTimes.reserve(static_cast<std::size_t>(count));
        std::size_t bodySamples = 0, faceSamples = 0;
        const auto add = [](std::vector<float>& sum, std::size_t& samples,
                            const std::vector<float>& embedding) {
            if (embedding.empty()
                || !std::all_of(embedding.begin(), embedding.end(),
                    [](float value) { return std::isfinite(value); })) return;
            if (sum.empty()) sum.assign(embedding.size(), 0.0F);
            if (sum.size() != embedding.size()) return;
            for (std::size_t i = 0; i < sum.size(); ++i) sum[i] += embedding[i];
            ++samples;
        };
        for (auto observation = first; observation != last; ++observation) {
            summary.observationTimes.push_back(observation->timestampSeconds);
            summary.duration += std::max(0.0, observation->frameDurationSeconds);
            summary.area += observation->box.area();
            add(summary.body, bodySamples, observation->appearanceEmbedding);
            add(summary.face, faceSamples, observation->faceEmbedding);
        }
        summary.area /= static_cast<double>(count);
        summary.bodyEvidence = std::min(1.0, static_cast<double>(bodySamples) / 3.0);
        summary.faceEvidence = std::min(1.0, static_cast<double>(faceSamples) / 3.0);
        summaries.push_back(std::move(summary));
        indices.push_back(index);
    }
    const auto local = selectDominantIdentities(summaries);
    for (std::size_t i = 0; i < local.size(); ++i) selected[indices[i]] = local[i];
    return selected;
}

} // namespace pfcore
