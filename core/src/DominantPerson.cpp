#include <pfcore/DominantPerson.hpp>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <numeric>
#include <stdexcept>
#include <span>
#include <cstdlib>
#include <cstdio>
#include <set>
#include <array>
#include <map>

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

bool sameObservedSkeleton(const PersonDetection& a, const PersonDetection& b)
{
    if (a.box.iou(b.box) < .30) return false;
    const double scale = std::sqrt(std::min(a.box.area(), b.box.area()));
    if (!(scale > 0) || !std::isfinite(scale)) return false;
    std::size_t shared = 0, torso = 0, head = 0, limbs = 0;
    double sum = 0, maximum = 0;
    for (std::size_t j = 0; j < std::min(a.keypoints.size(), b.keypoints.size()); ++j) {
        const auto& x = a.keypoints[j]; const auto& y = b.keypoints[j];
        if (!(x.confidence >= .5 && y.confidence >= .5)
            || !std::isfinite(x.x) || !std::isfinite(x.y)
            || !std::isfinite(y.x) || !std::isfinite(y.y)) continue;
        const double distance = std::hypot(x.x-y.x, x.y-y.y) / scale;
        ++shared; sum += distance; maximum = std::max(maximum, distance);
        if (j < 5) ++head;
        else if (j == 5 || j == 6 || j == 11 || j == 12) ++torso;
        else if (j < 17) ++limbs;
    }
    // Coincident boxes alone can be two crossing people. Require several
    // independently visible joints across torso and another anatomical region.
    return shared >= 5 && torso >= 2 && (head >= 2 || limbs >= 2)
        && sum/shared <= .015 && maximum <= .04;
}

IdentitySummary summarizeObservations(std::span<const PersonDetection> observations)
{
    IdentitySummary summary;
    if (observations.empty()) return summary;
    summary.observationTimes.reserve(observations.size());
    std::size_t bodySamples = 0, faceSamples = 0;
    const auto add = [](std::vector<float>& sum, std::size_t& samples,
                        const std::vector<float>& embedding) {
        if (embedding.empty() || !std::all_of(embedding.begin(), embedding.end(),
            [](float value) { return std::isfinite(value); })) return;
        if (sum.empty()) sum.assign(embedding.size(), 0.0F);
        if (sum.size() != embedding.size()) return;
        for (std::size_t i = 0; i < sum.size(); ++i) sum[i] += embedding[i];
        ++samples;
    };
    for (const auto& observation : observations) {
        summary.observationTimes.push_back(observation.timestampSeconds);
        summary.duration += std::max(0.0, observation.frameDurationSeconds);
        summary.area += observation.box.area();
        add(summary.body, bodySamples, observation.appearanceEmbedding);
        add(summary.face, faceSamples, observation.faceEmbedding);
    }
    summary.area /= static_cast<double>(observations.size());
    summary.bodyEvidence = std::min(1.0, static_cast<double>(bodySamples) / 3.0);
    summary.faceEvidence = std::min(1.0, static_cast<double>(faceSamples) / 3.0);
    return summary;
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

DominantPersonTracker::DominantPersonTracker(double iouThreshold, double maxGapSeconds, bool identityRetirement)
    : iouThreshold_(iouThreshold)
    , maxGapSeconds_(maxGapSeconds)
    , identityRetirement_(identityRetirement)
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
    retiredTracks_.clear();
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
    // Keep every raw observation. A stronger duplicate supplies a veto only,
    // never a copied embedding or an extra identity-confirmation sample.
    std::vector<std::size_t> identityWitness(detections.size());
    std::iota(identityWitness.begin(), identityWitness.end(), 0);
    for (std::size_t i = 0; identityRetirement_ && i < detections.size(); ++i) {
        for (std::size_t j = 0; j < detections.size(); ++j) {
            if (detections[j].confidence < .4
                || detections[j].confidence <= detections[identityWitness[i]].confidence
                || (detections[j].faceEmbedding.empty() && detections[j].appearanceEmbedding.empty())) continue;
            if (sameObservedSkeleton(detections[i], detections[j])) identityWitness[i] = j;
        }
    }

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
        if (retiredTracks_[track]) continue;
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
        bool identityConflict = false, viableContinuation = false;
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
            const bool gated = iou >= iouThreshold_
                || (center >= 0.42 && keypoints >= 0.38);
            if (!gated) continue;
            const auto& witness = detections[identityWitness[detection]];
            const double witnessFace = faceReference
                ? embeddingScore(faceReference->faceEmbedding, witness.faceEmbedding) : -1;
            const double witnessBody = appearanceReference
                ? embeddingScore(appearanceReference->appearanceEmbedding, witness.appearanceEmbedding) : -1;
            const bool conflict = (face > -1 && face < .363)
                || (face <= -1 && appearance >= 0 && appearance < .45)
                || (witnessFace > -1 && witnessFace < .363)
                || (witnessFace <= -1 && witnessBody >= 0 && witnessBody < .45);
            if (conflict) { identityConflict = true; continue; }
            viableContinuation = true;
            const double score = 0.48 * iou + 0.22 * center + 0.18 * keypoints
                + (appearance >= 0.0 ? 0.12 * std::max(0.0, appearance) : 0.0);
            candidates.push_back({score, track, detection});
        }
        if (identityRetirement_ && identityConflict && !viableContinuation) retiredTracks_[track] = true;
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
        retiredTracks_.push_back(false);
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

DominantVideoSelection selectDominantVideoWindows(const std::vector<MotionWindow>& windows)
{
    struct Source {
        std::map<std::size_t, const MotionWindow*> shots;
        std::vector<float> face;
        std::vector<std::vector<float>> anchors;
        double duration = 0;
    };
    // Ordered source IDs also make ties independent of file loading order.
    std::map<std::string, Source> evidence;
    for (const auto& w : windows) {
        auto& s = evidence[w.sourceId];
        if (!w.hasSceneIndex || w.frames.empty()) continue;
        auto [slot, inserted] = s.shots.try_emplace(w.sceneIndex, &w);
        const auto strength = [](const MotionWindow& v) {
            return v.faceConfidence >= .45 && embeddingScore(v.faceEmbedding, v.faceEmbedding) > .99
                ? v.faceConfidence : -1.0;
        };
        if (!inserted && strength(w) > strength(*slot->second)) slot->second = &w;
    }
    DominantVideoSelection result;
    result.suppliedSources = evidence.size();
    result.windows.assign(windows.size(), true);
    if (evidence.size() <= 1) {
        for (const auto& [name, _] : evidence) result.sources.push_back(name);
        return result;
    }
    std::vector<std::string> names;
    std::vector<Source*> sources;
    for (auto& [name, s] : evidence) {
        names.push_back(name); sources.push_back(&s);
        for (const auto& [_, w] : s.shots) {
            const double span = w->sceneEndSeconds - w->sceneStartSeconds;
            s.duration += std::max(0.0, span > 0 ? span
                : w->frames.back().timestampSeconds - w->frames.front().timestampSeconds);
            if (w->faceConfidence < .45 || embeddingScore(w->faceEmbedding,w->faceEmbedding) <= .99) continue;
            auto anchor = w->faceEmbedding;
            double norm = 0;
            for (const float v : anchor) norm += v*v;
            for (float& v : anchor) v /= std::sqrt(norm);
            if (s.face.empty()) s.face.assign(anchor.size(), 0);
            if (anchor.size() != s.face.size()) continue;
            for (std::size_t i = 0; i < anchor.size(); ++i) s.face[i] += anchor[i];
            s.anchors.push_back(std::move(anchor));
        }
    }
    const auto agrees = [&](std::size_t a, std::size_t b) {
        const auto& x = *sources[a]; const auto& y = *sources[b];
        // File-wide identity admission needs repeated independent shots and
        // stronger aggregate agreement than an isolated pairwise face link.
        if (x.anchors.size() < 2 || y.anchors.size() < 2
            || embeddingScore(x.face,y.face) < .60) return false;
        const auto majority = [](const Source& from, const Source& to) {
            const auto n = std::count_if(from.anchors.begin(),from.anchors.end(),[&](const auto& f) {
                return embeddingScore(f,to.face) >= .363;
            });
            return n * 4 >= static_cast<std::ptrdiff_t>(from.anchors.size()) * 3;
        };
        return majority(x,y) && majority(y,x);
    };
    if(std::getenv("PF_DEBUG_ANALYSIS")) {
        for(std::size_t i=0;i<sources.size();++i)
            std::fprintf(stderr,"PF_DEBUG_SOURCE_IDENTITY source=%s shots=%zu face_anchors=%zu\n",
                names[i].c_str(),sources[i]->shots.size(),sources[i]->anchors.size());
        for(std::size_t i=0;i<sources.size();++i)for(std::size_t j=i+1;j<sources.size();++j)
            std::fprintf(stderr,"PF_DEBUG_SOURCE_LINK a=%zu b=%zu face_cosine=%.3f accepted=%d\n",
                i,j,embeddingScore(sources[i]->face,sources[j]->face),agrees(i,j)?1:0);
    }
    // Build complete-link groups for the small source list. Merge only if
    // every member agrees with every other member;
    // a weak intermediate must never silently bridge incompatible identities.
    std::vector<std::vector<std::size_t>> groups;
    for (std::size_t i = 0; i < sources.size(); ++i) groups.push_back({i});
    struct Edge { std::size_t a,b; double score; };
    std::vector<Edge> edges;
    for (std::size_t i = 0; i < sources.size(); ++i) for (std::size_t j = i+1; j < sources.size(); ++j)
        if (agrees(i,j)) edges.push_back({i,j,embeddingScore(sources[i]->face,sources[j]->face)});
    std::stable_sort(edges.begin(),edges.end(),[](const Edge& a,const Edge& b){return a.score>b.score;});
    std::vector<std::size_t> owner(sources.size()); std::iota(owner.begin(),owner.end(),0);
    for (const auto& edge : edges) {
        auto a=owner[edge.a],b=owner[edge.b]; if (a==b) continue;
        bool valid=true;
        for (const auto x:groups[a]) for (const auto y:groups[b]) if (!agrees(x,y)) valid=false;
        if (!valid) continue;
        for (const auto member:groups[b]) {owner[member]=a;groups[a].push_back(member);}
        groups[b].clear();
    }
    std::size_t best=0; double duration=-1;
    for (std::size_t g=0;g<groups.size();++g) {
        double total=0; for (const auto i:groups[g]) total+=sources[i]->duration;
        if (groups[g].size()>groups[best].size()
            || (groups[g].size()==groups[best].size() && total>duration+1e-9)) {best=g;duration=total;}
    }
    for (const auto i:groups[best]) result.sources.push_back(names[i]);
    std::sort(result.sources.begin(),result.sources.end());
    for (std::size_t i=0;i<windows.size();++i)
        result.windows[i]=std::binary_search(result.sources.begin(),result.sources.end(),windows[i].sourceId);
    return result;
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

DominantSourceSelection selectDominantSourceTracks(const std::vector<PersonTrack>& tracks,
    const std::vector<PersonTrack>* rc16Tracks)
{
    DominantSourceSelection result;
    result.tracks.assign(tracks.size(), false);
    result.recovered.assign(tracks.size(), false);
    result.observationRuns.resize(tracks.size());
    std::vector<IdentitySummary> summaries;
    std::vector<std::size_t> indices;
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        if (tracks[i].observations.size() < 2) continue;
        summaries.push_back(summarizeObservations(tracks[i].observations));
        indices.push_back(i);
    }
    auto original = selectDominantIdentities(summaries);
    // Association corrections can change component membership even for an
    // unrelated short track. Preserve the actual rc16 admission, independently
    // replayed on the same detections, rather than treating reordered track IDs
    // as permission to admit a new unverified tail.
    using ObservationKey = std::array<double, 5>;
    const auto key = [](const PersonDetection& observation) -> ObservationKey {
        return {observation.timestampSeconds, observation.box.left, observation.box.top,
                observation.box.right, observation.box.bottom};
    };
    std::set<ObservationKey> previouslyAdmitted;
    if (rc16Tracks) {
        std::vector<IdentitySummary> previousSummaries;
        std::vector<std::size_t> previousIndices;
        for (std::size_t i = 0; i < rc16Tracks->size(); ++i) {
            if ((*rc16Tracks)[i].observations.size() < 2) continue;
            previousSummaries.push_back(summarizeObservations((*rc16Tracks)[i].observations));
            previousIndices.push_back(i);
        }
        const auto previous = selectDominantIdentities(previousSummaries);
        for (std::size_t i = 0; i < previous.size(); ++i) if (previous[i])
            for (const auto& observation : (*rc16Tracks)[previousIndices[i]].observations)
                previouslyAdmitted.insert(key(observation));
        for (std::size_t i = 0; i < original.size(); ++i) if (original[i]) {
            const auto& observations = tracks[indices[i]].observations;
            original[i] = std::any_of(observations.begin(), observations.end(),
                [&](const auto& observation) { return previouslyAdmitted.contains(key(observation)); });
        }
    }
    const auto faceObserved = [&](std::size_t i) {
        return summaries[i].faceEvidence > 0
            && embeddingScore(summaries[i].face, summaries[i].face) > .99;
    };
    const auto faceReady = [&](std::size_t i) { return faceObserved(i) && summaries[i].faceEvidence >= .45; };
    const auto bodyReady = [&](std::size_t i) {
        return summaries[i].bodyEvidence >= .45
            && embeddingScore(summaries[i].body, summaries[i].body) > .99;
    };
    const auto coVisible = [&](std::size_t a, std::size_t b) {
        const auto& left = summaries[a].observationTimes;
        const auto& right = summaries[b].observationTimes;
        std::size_t i = 0, j = 0;
        while (i < left.size() && j < right.size()) {
            if (std::abs(left[i] - right[j]) <= 1e-6) return true;
            if (left[i] < right[j]) ++i; else ++j;
        }
        return false;
    };
    std::vector<std::size_t> lead, anchors;
    for (std::size_t i = 0; i < original.size(); ++i) if (original[i]) {
        lead.push_back(i);
        if (faceReady(i)) anchors.push_back(i);
        result.tracks[indices[i]] = true;
        const auto& observations = tracks[indices[i]].observations;
        auto& runs = result.observationRuns[indices[i]];
        for (std::size_t j = 0; j < observations.size();) {
            if (rc16Tracks && !previouslyAdmitted.contains(key(observations[j]))) { ++j; continue; }
            const auto begin = j++;
            while (j < observations.size()
                && (!rc16Tracks || previouslyAdmitted.contains(key(observations[j])))) ++j;
            runs.push_back({begin, j});
        }
    }
    if (anchors.size() < 2) return result;
    std::vector<bool> candidate(summaries.size(), false), ambiguous(summaries.size(), false);
    for (std::size_t i = 0; i < summaries.size(); ++i) {
        if (original[i] || !faceReady(i) || !bodyReady(i)) continue;
        if (std::any_of(lead.begin(), lead.end(), [&](std::size_t anchor) {
            return coVisible(i, anchor) || (faceObserved(anchor) && !faceReady(anchor)
                && embeddingScore(summaries[i].face, summaries[anchor].face) < .363);
        })) continue;
        std::size_t votes = 0;
        bool body = false;
        for (const auto anchor : anchors) {
            if (embeddingScore(summaries[i].face, summaries[anchor].face) < .363) continue;
            ++votes;
            body = body || (bodyReady(anchor)
                && embeddingScore(summaries[i].body, summaries[anchor].body) >= .76);
        }
        candidate[i] = votes >= 2 && votes > anchors.size() / 2 && body;
    }
    for (std::size_t i = 0; i < candidate.size(); ++i) if (candidate[i])
        for (std::size_t j = i + 1; j < candidate.size(); ++j) if (candidate[j] && coVisible(i, j))
            ambiguous[i] = ambiguous[j] = true;
    const bool trace = std::getenv("PF_DEBUG_DOMINANT") != nullptr;
    for (std::size_t i = 0; i < candidate.size(); ++i) {
        if (!candidate[i] || ambiguous[i]) continue;
        const auto trackIndex = indices[i];
        const auto& observations = tracks[trackIndex].observations;
        auto& ranges = result.observationRuns[trackIndex];
        std::size_t first = 0, last = 0, independentFaces = 0;
        const auto finish = [&] {
            if (independentFaces >= 2) {
                // The .76 body gate is calibrated for a multi-crop prototype,
                // not a single image. Recompute it INSIDE this supported run;
                // a different person's unverified tail must not contribute.
                const auto run = summarizeObservations(std::span(observations).subspan(first, last - first + 1));
                std::size_t votes = 0;
                bool body = false;
                if (run.bodyEvidence >= .45 && embeddingScore(run.body, run.body) > .99) {
                    for (const auto anchor : anchors) {
                        if (embeddingScore(run.face, summaries[anchor].face) < .363) continue;
                        ++votes;
                        body = body || (bodyReady(anchor)
                            && embeddingScore(run.body, summaries[anchor].body) >= .76);
                    }
                }
                if (votes >= 2 && votes > anchors.size() / 2 && body) ranges.push_back({first, last + 1});
            }
            independentFaces = 0;
        };
        // Same maximum gap as default geometric tracking. Bound interpolation
        // between identity observations; never extrapolate before/after them.
        constexpr double maximumIdentityGapSeconds = 1.0;
        for (std::size_t j = 0; j < observations.size(); ++j) {
            const auto& observation = observations[j];
            if (independentFaces && observation.timestampSeconds - observations[last].timestampSeconds
                    > maximumIdentityGapSeconds) finish();
            if (observation.faceEmbedding.empty()) continue;
            std::size_t votes = 0;
            for (const auto anchor : anchors) {
                if (embeddingScore(observation.faceEmbedding, summaries[anchor].face) < .363) continue;
                ++votes;
            }
            if (votes < 2 || votes <= anchors.size() / 2) { finish(); continue; }
            if (!independentFaces) first = j;
            else if (observation.timestampSeconds <= observations[last].timestampSeconds + 1e-6) continue;
            last = j;
            ++independentFaces;
        }
        finish();
        result.tracks[trackIndex] = result.recovered[trackIndex] = !ranges.empty();
        if (trace) for (const auto range : ranges)
            std::fprintf(stderr, "PF_DOMINANT track=%zu decision=verified-run begin=%zu end=%zu time=%.9f/%.9f\n",
                tracks[trackIndex].id, range.begin, range.end,
                observations[range.begin].timestampSeconds, observations[range.end - 1].timestampSeconds);
    }
    return result;
}

} // namespace pfcore
