#include <pfcore/DominantPerson.hpp>
#include <pfcore/FaceConsensus.hpp>

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
#include <limits>
#include <atomic>
#include <future>
#include <thread>
#include <chrono>

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

// A close-up can expose only a head. This is used solely to distinguish
// overlapping observations during recovery, never to suppress detections.
static bool sameObservedHead(const PersonDetection& a, const PersonDetection& b)
{
    const double overlap=a.box.iou(b.box);
    if (!std::isfinite(overlap) || overlap < .30 || a.keypoints.size() < 5 || b.keypoints.size() < 5) return false;
    if (!a.faceEmbedding.empty() && !b.faceEmbedding.empty()
        && embeddingScore(a.faceEmbedding,b.faceEmbedding) < .5) return false;
    const double scale=std::sqrt(std::min(a.box.area(),b.box.area()));
    if (!(scale > 0) || !std::isfinite(scale)) return false;
    std::size_t count=0; double sum=0, maximum=0;
    for(std::size_t i=0;i<5;++i) {
        const auto& x=a.keypoints[i]; const auto& y=b.keypoints[i];
        if(!(x.confidence >= .9 && y.confidence >= .9) || !std::isfinite(x.x+x.y+y.x+y.y)) continue;
        const double distance=std::hypot(x.x-y.x,x.y-y.y)/scale;
        ++count; sum+=distance; maximum=std::max(maximum,distance);
    }
    if(count<4 || sum/count>.012 || maximum>.02) return false;
    for(const auto* detection:{&a,&b}) {
        const auto& left=detection->keypoints[1]; const auto& right=detection->keypoints[2];
        if(!(left.confidence >= .9 && right.confidence >= .9)
            || !std::isfinite(left.x+left.y+right.x+right.y)
            || std::hypot(left.x-right.x,left.y-right.y)/scale<.03) return false;
    }
    return true;
}

IdentitySummary summarizeObservations(std::span<const PersonDetection> observations, bool uniqueTimestamps = false)
{
    IdentitySummary summary;
    if (observations.empty()) return summary;
    summary.observationTimes.reserve(observations.size());
    std::size_t bodySamples = 0, faceSamples = 0;
    std::set<double> bodyTimes, faceTimes;
    const auto add = [](std::vector<float>& sum, std::size_t& samples,
                        const std::vector<float>& embedding) -> bool {
        if (embedding.empty() || !std::all_of(embedding.begin(), embedding.end(),
            [](float value) { return std::isfinite(value); })) return false;
        if (sum.empty()) sum.assign(embedding.size(), 0.0F);
        if (sum.size() != embedding.size()) return false;
        for (std::size_t i = 0; i < sum.size(); ++i) sum[i] += embedding[i];
        ++samples;
        return true;
    };
    for (const auto& observation : observations) {
        summary.observationTimes.push_back(observation.timestampSeconds);
        summary.duration += std::max(0.0, observation.frameDurationSeconds);
        summary.area += observation.box.area();
        // Repeated records from a single frame are not independent evidence.
        // Count each modality only once per finite observation timestamp.
        if (!uniqueTimestamps) {
            add(summary.body, bodySamples, observation.appearanceEmbedding);
            add(summary.face, faceSamples, observation.faceEmbedding);
        } else if (std::isfinite(observation.timestampSeconds)) {
            if (!bodyTimes.contains(observation.timestampSeconds)
                && add(summary.body, bodySamples, observation.appearanceEmbedding))
                bodyTimes.insert(observation.timestampSeconds);
            if (!faceTimes.contains(observation.timestampSeconds)
                && add(summary.face, faceSamples, observation.faceEmbedding))
                faceTimes.insert(observation.timestampSeconds);
        }
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

DominantPersonTracker::DominantPersonTracker(double iouThreshold, double maxGapSeconds, bool identityRetirement,double minimumBodySimilarity)
    : iouThreshold_(iouThreshold)
    , maxGapSeconds_(maxGapSeconds)
    , identityRetirement_(identityRetirement)
    , minimumBodySimilarity_(minimumBodySimilarity)
{
    if (!(iouThreshold_ >= 0.0 && iouThreshold_ <= 1.0)) {
        throw std::invalid_argument("DominantPersonTracker: IoU threshold must be in [0, 1]");
    }
    if (!(maxGapSeconds_ > 0.0)) {
        throw std::invalid_argument("DominantPersonTracker: max gap must be > 0");
    }
    if(!(minimumBodySimilarity_>=0&&minimumBodySimilarity_<=1))throw std::invalid_argument("PersonTracker: body similarity must be in [0,1]");
}

void DominantPersonTracker::reset()
{
    tracks_.clear();
    retiredTracks_.clear();
    nextId_ = 1;
    sceneTrackStart_ = 0;
}

std::size_t DominantPersonTracker::recoverConsistentWeakFaces()
{
    std::size_t recovered = 0;
    for (auto& track : tracks_) {
        std::vector<std::size_t> samples;
        for (std::size_t i = 0; i < track.observations.size(); ++i) {
            const auto& observation = track.observations[i];
            if (observation.weakFaceEmbedding.empty() || !std::isfinite(observation.timestampSeconds)) continue;
            if (!samples.empty() && observation.timestampSeconds
                    - track.observations[samples.back()].timestampSeconds < .15) continue;
            samples.push_back(i);
        }
        if (samples.size() < 3 || track.observations[samples.back()].timestampSeconds
                - track.observations[samples.front()].timestampSeconds < .3) continue;
        bool consistent = true;
        for (std::size_t i = 0; consistent && i < samples.size(); ++i) {
            const auto& face = track.observations[samples[i]].weakFaceEmbedding;
            for (std::size_t j = i+1; j < samples.size(); ++j)
                if (embeddingScore(face,track.observations[samples[j]].weakFaceEmbedding) < .70) {
                    consistent = false; break;
                }
            for (const auto& observation : track.observations)
                if (!observation.faceEmbedding.empty() && embeddingScore(face,observation.faceEmbedding) < .50) {
                    consistent = false; break;
                }
        }
        if (!consistent) continue;
        for (const auto i : samples) {
            track.observations[i].faceEmbedding = std::move(track.observations[i].weakFaceEmbedding);
            ++recovered;
        }
    }
    return recovered;
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
                || (face <= -1 && appearance >= 0 && appearance < minimumBodySimilarity_)
                || (witnessFace > -1 && witnessFace < .363)
                || (witnessFace <= -1 && witnessBody >= 0 && witnessBody < minimumBodySimilarity_);
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

static std::vector<bool> selectIdentityComponents(const std::vector<IdentitySummary>& identities,
    const DominantSelectionControl& control, std::vector<std::vector<std::size_t>>* components,
    double faceLink = .363, double bodyLink = .76)
{
    const auto count = identities.size();
    std::vector<bool> selected(count, false);
    if (!count) return selected;
    const auto cancelled=[&] {return control.cancelled && control.cancelled();};
    if(control.progress)control.progress(DominantSelectionStage::Link,0,count);
    if(cancelled())return selected;
    // Identity evidence is immutable during clustering. Validate each prototype
    // once, instead of rescanning 512-dimensional embeddings for every edge.
    std::vector<bool> observedFace(count), readyFace(count), readyBody(count);
    for (std::size_t i=0;i<count;++i) {
        observedFace[i]=identities[i].faceEvidence>0.0
            && embeddingScore(identities[i].face,identities[i].face)>0.99;
        readyFace[i]=identities[i].faceEvidence>=0.45 && observedFace[i];
        readyBody[i]=identities[i].bodyEvidence>=0.45
            && embeddingScore(identities[i].body,identities[i].body)>0.99;
    }
    const auto faceObserved=[&](std::size_t i) {return observedFace[i];};
    const auto faceReady=[&](std::size_t i) {return readyFace[i];};
    const auto bodyReady=[&](std::size_t i) {return readyBody[i];};
    std::vector<double> faceNorm(count),bodyNorm(count);
    const auto norm=[](const std::vector<float>& values) {
        double result=0;
        for(const float v:values) {if(!std::isfinite(v))return -1.0;result+=static_cast<double>(v)*v;}
        return result;
    };
    for(std::size_t i=0;i<count;++i) {faceNorm[i]=norm(identities[i].face);bodyNorm[i]=norm(identities[i].body);}
    const auto prototypeScore=[&](std::size_t a,std::size_t b,bool face) {
        const auto& left=face ? identities[a].face : identities[a].body;
        const auto& right=face ? identities[b].face : identities[b].body;
        const auto& norms=face ? faceNorm : bodyNorm;
        if(left.empty() || left.size()!=right.size() || norms[a]<=1e-12 || norms[b]<=1e-12)return -1.0;
        double dot=0;for(std::size_t i=0;i<left.size();++i)dot+=static_cast<double>(left[i])*right[i];
        return std::clamp(dot/std::sqrt(norms[a]*norms[b]),-1.0,1.0);
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
    // Cluster merges used to recompute the same face distances repeatedly.
    // One bit per pair records the exact veto, with a fixed 32 MiB ceiling.
    // Each row belongs to one worker, so packed writes never race.
    const auto wordsPerRow=(count+63)/64;
    constexpr std::size_t conflictBudget=32U*1024U*1024U;
    const bool memoConflicts=count<=conflictBudget/(sizeof(std::uint64_t)*wordsPerRow);
    std::vector<std::uint64_t> conflicts(memoConflicts ? count*wordsPerRow : 0);
    std::atomic_size_t nextRow{0}, completedRows{0};
    const auto scan=[&](bool serial) {
        std::vector<Edge> found;
        for (;;) {
            const auto a=nextRow.fetch_add(1);
            if(a>=count || cancelled())break;
            if(serial && control.progress)control.progress(DominantSelectionStage::Link,a,count);
            for(std::size_t b=a+1;b<count;++b) {
                if(b%256==0 && cancelled())break;
                const bool visible=coVisible(a,b);
                const bool observed=faceObserved(a) && faceObserved(b);
                const bool face=faceReady(a) && faceReady(b);
                const double faceScore=(face || (memoConflicts && observed)) ? prototypeScore(a,b,true) : -1.;
                if(memoConflicts && (visible || (observed && faceScore<.363)))
                    conflicts[a*wordsPerRow+b/64]|=std::uint64_t{1}<<(b%64);
                if(visible || (!face && !(bodyReady(a) && bodyReady(b))))continue;
                const double score=face ? faceScore : prototypeScore(a,b,false);
                if(score >= (face ? faceLink : bodyLink))found.push_back({a,b,score,face});
            }
            ++completedRows;
        }
        return found;
    };
    std::vector<Edge> edges;
    const auto workerCount=count<256 ? 1U : std::clamp<std::size_t>(control.maxThreads
        ? control.maxThreads : std::thread::hardware_concurrency(),1,24);
    if(workerCount==1)edges=scan(true);
    else {
        std::vector<std::future<std::vector<Edge>>> jobs;
        for(std::size_t i=0;i<workerCount;++i)jobs.push_back(std::async(std::launch::async,[&]{return scan(false);}));
        for(auto& job:jobs) {
            while(job.wait_for(std::chrono::milliseconds(100))!=std::future_status::ready)
                if(control.progress)control.progress(DominantSelectionStage::Link,completedRows.load(),count);
            auto found=job.get();edges.insert(edges.end(),std::make_move_iterator(found.begin()),std::make_move_iterator(found.end()));
        }
    }
    if(control.progress)control.progress(DominantSelectionStage::Link,count,count);
    if(cancelled())return selected;
    if(control.progress)control.progress(DominantSelectionStage::Cluster,0,edges.size());
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
    if(control.progress)control.progress(DominantSelectionStage::Cluster,0,edges.size());
    std::size_t linked=0;
    for (const auto& edge : edges) {
        if((linked++ % 256)==0) {
            if(cancelled())return selected;
            if(control.progress)control.progress(DominantSelectionStage::Cluster,linked-1,edges.size());
        }
        const auto a = owner[edge.a], b = owner[edge.b];
        if (a == b) continue;
        bool conflict = false;
        for (const auto left : members[a]) {
            for (const auto right : members[b]) {
                // A single sampled face cannot establish a positive identity
                // link, but contradictory facial evidence must still veto a
                // clothing bridge. Otherwise a short extra joins the lead
                // simply because its face has not reached two samples yet.
                const auto lo=std::min(left,right),hi=std::max(left,right);
                const bool incompatible=memoConflicts
                    ? (conflicts[lo*wordsPerRow+hi/64] & (std::uint64_t{1}<<(hi%64)))!=0
                    : coVisible(left,right) || (faceObserved(left) && faceObserved(right)
                        && prototypeScore(left,right,true)<.363);
                if (incompatible) {
                    conflict = true; break;
                }
            }
            if (conflict) break;
        }
        if (conflict) continue;
        for (const auto i : members[b]) { owner[i] = a; members[a].push_back(i); }
        members[b].clear();
    }
    if(control.progress)control.progress(DominantSelectionStage::Cluster,edges.size(),edges.size());
    if(cancelled())return selected;
    if (components) {
        for (const auto& group : members) if (!group.empty()) components->push_back(group);
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

std::vector<bool> selectDominantIdentities(const std::vector<IdentitySummary>& identities,
    const DominantSelectionControl& control)
{
    return selectIdentityComponents(identities, control, nullptr);
}

static bool coVisibleDifferent(const std::vector<const PersonDetection*>& x,
                               const std::vector<const PersonDetection*>& y)
{
    std::size_t i=0,j=0;
    while(i<x.size() && j<y.size()) {
        const auto xt=x[i]->timestampSeconds,yt=y[j]->timestampSeconds;
        if(std::abs(xt-yt)<=1e-6) {
            std::size_t ie=i+1,je=j+1;
            while(ie<x.size() && std::abs(x[ie]->timestampSeconds-xt)<=1e-6)++ie;
            while(je<y.size() && std::abs(y[je]->timestampSeconds-yt)<=1e-6)++je;
            // A second detector box on the same skeleton is not a second
            // person. Any genuinely distinct observation still vetoes.
            for(auto a=i;a<ie;++a)for(auto b=j;b<je;++b)
                if(!sameObservedSkeleton(*x[a],*y[b]) && !sameObservedHead(*x[a],*y[b]))return true;
            i=ie;j=je;
        } else if(xt<yt)++i;else ++j;
    }
    return false;
}

// A partial close-up can expose only one head and one torso joint. This is
// insufficient on its own; the caller also requires very strong body agreement
// and verifies every overlapping frame before joining two short fragments.
static bool samePartialObservedSkeleton(const PersonDetection& a,const PersonDetection& b)
{
    const auto intersection=std::max(0.,std::min(a.box.right,b.box.right)-std::max(a.box.left,b.box.left))
        *std::max(0.,std::min(a.box.bottom,b.box.bottom)-std::max(a.box.top,b.box.top));
    const double smaller=std::min(a.box.area(),b.box.area());
    if(!std::isfinite(smaller) || !std::isfinite(intersection) || smaller<=0 || intersection/smaller<.97)return false;
    const double scale=std::sqrt(smaller);std::size_t head=0,torso=0,count=0;double sum=0,maximum=0;
    for(std::size_t j=0;j<std::min(a.keypoints.size(),b.keypoints.size());++j) {
        const auto& x=a.keypoints[j];const auto& y=b.keypoints[j];
        if(x.confidence<.8 || y.confidence<.8 || !std::isfinite(x.x+x.y+y.x+y.y))continue;
        if(j>=5 && j!=5 && j!=6 && j!=11 && j!=12)continue;
        const double distance=std::hypot(x.x-y.x,x.y-y.y)/scale;
        if(j<5)++head;else ++torso;
        ++count;sum+=distance;maximum=std::max(maximum,distance);
    }
    return head>=1 && torso>=1 && count>=2 && sum/count<=.07 && maximum<=.10;
}

// Track means are useful seeds, but are not a multi-view identity model: a
// profile can disagree with a frontal mean of the very same person. Recover
// links using repeated, independent observed faces, never a clothing chain.
static void recoverCharacterViews(std::vector<std::vector<std::size_t>>& components,
    const std::vector<PersonTrack>& tracks, const std::vector<std::size_t>& indices,
    const DominantSelectionControl& control)
{
    struct Gallery {
        std::vector<const PersonDetection*> faces;
        std::vector<const PersonDetection*> observations;
        std::vector<float> body;
    };
    const auto originalComponents=components;
    std::vector<std::vector<const PersonDetection*>> atomicFaces(indices.size());
    std::vector<std::vector<const PersonDetection*>> atomicLocalFaces(indices.size());
    std::vector<std::vector<float>> atomicBodies(indices.size());
    for(std::size_t i=0;i<indices.size();++i) {
        auto& faces=atomicFaces[i];
        for(const auto& o:tracks[indices[i]].observations)
            if(std::isfinite(o.timestampSeconds) && embeddingScore(o.faceEmbedding,o.faceEmbedding)>.99)
                faces.push_back(&o);
        std::stable_sort(faces.begin(),faces.end(),[](auto a,auto b){return a->timestampSeconds<b->timestampSeconds;});
        auto& local=atomicLocalFaces[i];
        for(std::size_t j=0;j<faces.size();) {
            std::size_t end=j+1;
            while(end<faces.size() && std::abs(faces[end]->timestampSeconds-faces[j]->timestampSeconds)<1e-6)++end;
            bool ambiguous=false;
            for(auto k=j+1;k<end;++k)
                if(faces[k]->faceEmbedding!=faces[j]->faceEmbedding)ambiguous=true;
            if(!ambiguous && (local.empty() || faces[j]->timestampSeconds-local.back()->timestampSeconds>=.1-1e-9))
                local.push_back(faces[j]);
            j=end;
        }
        if(local.size()>64) {
            auto full=std::move(local);
            local.clear();
            for(std::size_t j=0;j<64;++j)local.push_back(full[j*(full.size()-1)/63]);
        }
        std::vector<const PersonDetection*> distinct;
        for(auto face:faces)if(distinct.empty() || face->timestampSeconds-distinct.back()->timestampSeconds>=.1-1e-9)
            distinct.push_back(face);
        faces.clear();
        constexpr std::size_t limit=64;
        for(std::size_t j=0;j<std::min(limit,distinct.size());++j)
            faces.push_back(distinct[distinct.size()<=limit ? j : j*(distinct.size()-1)/(limit-1)]);
        if(local.size()>=3)
            atomicBodies[i]=summarizeObservations(tracks[indices[i]].observations,true).body;
    }
    std::vector<Gallery> galleries(components.size());
    for (std::size_t i=0;i<components.size();++i) {
        auto& gallery=galleries[i];
        for (auto index:components[i]) for (const auto& observation:tracks[indices[index]].observations) {
            if(std::isfinite(observation.timestampSeconds))gallery.observations.push_back(&observation);
            if (std::isfinite(observation.timestampSeconds)
                && embeddingScore(observation.faceEmbedding,observation.faceEmbedding)>.99)
                gallery.faces.push_back(&observation);
            const auto& body=observation.appearanceEmbedding;
            if (embeddingScore(body,body)>.99 && (gallery.body.empty() || gallery.body.size()==body.size())) {
                if(gallery.body.empty())gallery.body.resize(body.size());
                for(std::size_t j=0;j<body.size();++j)gallery.body[j]+=body[j];
            }
        }
        std::stable_sort(gallery.observations.begin(),gallery.observations.end(),
            [](auto a,auto b){return a->timestampSeconds<b->timestampSeconds;});
        std::stable_sort(gallery.faces.begin(),gallery.faces.end(),[](auto a,auto b){return a->timestampSeconds<b->timestampSeconds;});
        std::vector<const PersonDetection*> independent;
        for(std::size_t j=0;j<gallery.faces.size();) {
            std::size_t end=j+1;
            while(end<gallery.faces.size()
                && std::abs(gallery.faces[end]->timestampSeconds-gallery.faces[j]->timestampSeconds)<1e-6)++end;
            bool ambiguous=false;
            for(auto k=j+1;k<end;++k)
                if(gallery.faces[k]->faceEmbedding!=gallery.faces[j]->faceEmbedding)ambiguous=true;
            const auto* observation=gallery.faces[j];
            if(!ambiguous && (independent.empty()
                || observation->timestampSeconds-independent.back()->timestampSeconds>=.1-1e-9))
                independent.push_back(observation);
            j=end;
        }
        // Bound comparisons while retaining views across the complete history.
        constexpr std::size_t limit=64;
        gallery.faces.clear();
        for(std::size_t j=0;j<std::min(limit,independent.size());++j)
            gallery.faces.push_back(independent[independent.size()<=limit ? j : j*(independent.size()-1)/(limit-1)]);
    }
    const auto concurrent=[&](std::size_t a,std::size_t b) {
        return coVisibleDifferent(galleries[a].observations,galleries[b].observations);
    };
    std::map<std::pair<std::size_t,std::size_t>,bool> trackConflictCache;
    const auto strongTrackConflict=[&](std::size_t a,std::size_t b) {
        const auto ordered=std::minmax(a,b);
        const auto key=std::pair{ordered.first,ordered.second};
        if(const auto found=trackConflictCache.find(key);found!=trackConflictCache.end())return found->second;
        for(auto i:originalComponents[a])for(auto j:originalComponents[b]) {
            const auto& left=atomicFaces[i];const auto& right=atomicFaces[j];
            if(left.size()<3 || right.size()<3)continue;
            // Only the existence of a sufficiently similar pair matters for
            // this veto. Stop as soon as one is found instead of comparing
            // every remaining face in both tracks.
            bool compatible=false;
            for(auto x:left) {
                for(auto y:right)if(embeddingScore(x->faceEmbedding,y->faceEmbedding)>=.30) {
                    compatible=true;break;
                }
                if(compatible)break;
            }
            if(!compatible){trackConflictCache.emplace(key,true);return true;}
        }
        trackConflictCache.emplace(key,false);
        return false;
    };
    struct Link {std::size_t a,b;double score;bool repeated;};
    std::vector<Link> links;
    std::set<std::pair<std::size_t,std::size_t>> contradictions;
    for(std::size_t singleIndex=0;singleIndex<galleries.size();++singleIndex) {
        if(control.cancelled && control.cancelled())return;
        const auto& single=galleries[singleIndex];
        if(single.faces.size()!=1 || single.observations.size()<2)continue;
        // Do not treat duplicated frames or contradictory face detections as
        // independent evidence for a difficult, single-face appearance.
        double firstBodyTime=INFINITY; bool independentBody=false, conflictingFace=false;
        for(const auto* observation:single.observations) {
            if(embeddingScore(observation->appearanceEmbedding,observation->appearanceEmbedding)>.99) {
                firstBodyTime=std::min(firstBodyTime,observation->timestampSeconds);
                if(observation->timestampSeconds-firstBodyTime>=.1-1e-9)independentBody=true;
            }
            if(embeddingScore(observation->faceEmbedding,observation->faceEmbedding)>.99
               && embeddingScore(observation->faceEmbedding,single.faces.front()->faceEmbedding)<.5)
                conflictingFace=true;
        }
        if(!independentBody || conflictingFace)continue;
        struct Support {std::size_t group;double strength;std::size_t independent;};
        std::vector<Support> support;
        std::vector<double> groupBest(galleries.size(),-1.);
        for(std::size_t group=0;group<galleries.size();++group) {
            if(group==singleIndex)continue;
            std::vector<double> times;double strength=-1;
            for(const auto index:originalComponents[group]) {
                const auto& faces=atomicLocalFaces[index];
                if(faces.size()<3)continue;
                std::vector<double> scores;scores.reserve(faces.size());
                for(const auto* face:faces)scores.push_back(embeddingScore(single.faces.front()->faceEmbedding,face->faceEmbedding));
                std::sort(scores.begin(),scores.end(),std::greater<>());
                groupBest[group]=std::max(groupBest[group],scores[2]);
                if(scores[0]>=.58 && scores[2]>=.54 && embeddingScore(single.body,atomicBodies[index])>=.60) {
                    times.push_back(faces.front()->timestampSeconds);strength=std::max(strength,scores[2]);
                }
            }
            std::sort(times.begin(),times.end());
            double previous=-INFINITY;std::size_t independent=0;
            for(auto time:times)if(time-previous>=2.){++independent;previous=time;}
            if(independent>=3 && !concurrent(singleIndex,group))support.push_back({group,strength,independent});
        }
        if(support.size()!=1)continue;
        const auto chosen=support.front();double competitor=-1;
        for(std::size_t group=0;group<groupBest.size();++group)
            if(group!=singleIndex && group!=chosen.group)competitor=std::max(competitor,groupBest[group]);
        if(chosen.strength-competitor>=.10)
            links.push_back({singleIndex,chosen.group,chosen.strength,false});
    }
    for(std::size_t a=0;a<galleries.size();++a) {
        if(control.cancelled && control.cancelled())return;
        if(control.progress)control.progress(DominantSelectionStage::Recovery,a,galleries.size());
        const auto& x=galleries[a];
        if(x.faces.empty())continue;
        for(std::size_t b=0;b<a;++b) {
            if((b%32)==0 && control.cancelled && control.cancelled())return;
            const auto& y=galleries[b];
            if(y.faces.empty() || concurrent(a,b))continue;
            const double body=embeddingScore(x.body,y.body);
            if(x.faces.size()==1 || y.faces.size()==1) {
                const auto& single=x.faces.size()==1 ? x : y;
                const auto& many=x.faces.size()==1 ? y : x;
                if(single.observations.size()<3)continue;
                // A bounded whole-person gallery may omit a short matching
                // view, and its averaged clothing can mix many outfits. Check
                // a coherent individual track as a second route, requiring
                // both strong body agreement and three independent faces.
                const auto manyIndex=x.faces.size()==1 ? b : a;
                for(const auto index:originalComponents[manyIndex]) {
                    const auto& faces=atomicLocalFaces[index];
                    if(faces.size()<3 || embeddingScore(single.body,atomicBodies[index])<.90)continue;
                    std::vector<double> scores;
                    for(const auto* face:faces)
                        scores.push_back(embeddingScore(single.faces.front()->faceEmbedding,face->faceEmbedding));
                    std::sort(scores.begin(),scores.end(),std::greater<>());
                    if(scores[0]>=.65 && scores[2]>=.55) {
                        links.push_back({a,b,(scores[0]+scores[1]+scores[2])/3,false});
                        break;
                    }
                }
                if(many.faces.size()<3 || body<.78)continue;
                std::vector<double> scores;
                for(const auto* other:many.faces)
                    scores.push_back(embeddingScore(single.faces.front()->faceEmbedding,other->faceEmbedding));
                std::sort(scores.begin(),scores.end(),std::greater<>());
                // One image may corroborate a track only when at least three
                // independent reference images and body evidence agree.
                if(scores[0]>=.65 && scores[2]>=.55)
                    links.push_back({a,b,(scores[0]+scores[1]+scores[2])/3,false});
                continue;
            }
            std::vector<double> left(x.faces.size(),-1),right(y.faces.size(),-1);
            std::vector<double> leftBest(x.faces.size(),-1),rightBest(y.faces.size(),-1);
            for(std::size_t i=0;i<x.faces.size();++i)for(std::size_t j=0;j<y.faces.size();++j) {
                const double score=embeddingScore(x.faces[i]->faceEmbedding,y.faces[j]->faceEmbedding);
                if(score>leftBest[i]) {left[i]=leftBest[i];leftBest[i]=score;} else left[i]=std::max(left[i],score);
                if(score>rightBest[j]) {right[j]=rightBest[j];rightBest[j]=score;} else right[j]=std::max(right[j],score);
            }
            std::size_t l=0,r=0;double sum=0;
            for(double score:left)if(score>=.363){++l;sum+=score;}
            for(double score:right)if(score>=.363){++r;sum+=score;}
            // Each accepted face needs TWO independent matching references.
            // A single lucky pair, or repeated copies of one frame, cannot link.
            if(l<2 || r<2) {
                const auto best=std::max(*std::max_element(leftBest.begin(),leftBest.end()),
                                         *std::max_element(rightBest.begin(),rightBest.end()));
                if(x.faces.size()>=3 && y.faces.size()>=3 && best<.30)contradictions.emplace(b,a);
                continue;
            }
            const bool covered=x.faces.size()<=y.faces.size() ? l*5>=x.faces.size()*3 : r*5>=y.faces.size()*3;
            const double score=sum/(l+r);
            if(covered && (body>=.72 || (body>=.70 && score>=.52) || score>=.65))
                links.push_back({a,b,score,true});
        }
    }
    std::stable_sort(links.begin(),links.end(),[](const auto& a,const auto& b){return a.score>b.score;});
    std::vector<std::size_t> owner(components.size());std::iota(owner.begin(),owner.end(),0);
    std::vector<std::vector<std::size_t>> seeds(components.size());
    for(std::size_t i=0;i<seeds.size();++i)seeds[i].push_back(i);
    for(const auto& link:links) {
        if(control.cancelled && control.cancelled())return;
        const auto a=owner[link.a],b=owner[link.b];if(a==b)continue;
        bool conflict=false,weakFaceConflict=false;
        for(auto x:seeds[a])for(auto y:seeds[b]) {
            const auto pair=std::minmax(x,y);
            conflict=conflict || concurrent(x,y) || strongTrackConflict(x,y);
            weakFaceConflict=weakFaceConflict || contradictions.contains({pair.first,pair.second});
        }
        if(weakFaceConflict) {
            const auto prototype=[&](std::size_t group) {
                std::vector<float> sum;
                for(auto seed:seeds[group])for(const auto* observation:galleries[seed].faces) {
                    const auto& face=observation->faceEmbedding;
                    if(sum.empty())sum.resize(face.size());
                    if(sum.size()==face.size())for(std::size_t i=0;i<sum.size();++i)sum[i]+=face[i];
                }
                return sum;
            };
            // A weak negative from a single view cannot veto repeated
            // multi-view agreement, but one-face corroboration may not
            // override any observed contradiction.
            conflict=conflict || !link.repeated || link.score<.45
                || embeddingScore(prototype(a),prototype(b))<.363;
        }
        if(conflict)continue;
        components[a].insert(components[a].end(),components[b].begin(),components[b].end());components[b].clear();
        for(auto seed:seeds[b]) {owner[seed]=a;seeds[a].push_back(seed);}seeds[b].clear();
    }
    std::erase_if(components,[](const auto& c){return c.empty();});
}

// Some back/profile tracks have no face at all. Attach a single such track
// only when several independent body observations agree with one face-backed
// candidate and clearly outrank all other eligible face-backed candidates.
static void recoverUniqueBodyTracks(std::vector<std::vector<std::size_t>>& components,
    const std::vector<PersonTrack>& tracks,const std::vector<std::size_t>& indices,
    const DominantSelectionControl& control)
{
    struct Evidence {
        std::vector<const PersonDetection*> observations,body,face;
        std::vector<float> prototype;
        std::size_t faces=0;
    };
    std::vector<Evidence> evidence(components.size());
    for(std::size_t i=0;i<components.size();++i) {
        auto& e=evidence[i];
        for(auto index:components[i])for(const auto& o:tracks[indices[index]].observations) {
            if(!std::isfinite(o.timestampSeconds))continue;
            e.observations.push_back(&o);
            if(embeddingScore(o.faceEmbedding,o.faceEmbedding)>.99)e.face.push_back(&o);
            if(embeddingScore(o.appearanceEmbedding,o.appearanceEmbedding)>.99)e.body.push_back(&o);
        }
        std::stable_sort(e.observations.begin(),e.observations.end(),[](auto a,auto b){return a->timestampSeconds<b->timestampSeconds;});
        std::stable_sort(e.face.begin(),e.face.end(),[](auto a,auto b){return a->timestampSeconds<b->timestampSeconds;});
        double faceTime=-std::numeric_limits<double>::infinity();
        for(auto o:e.face)if(o->timestampSeconds-faceTime>=.1-1e-9) {++e.faces;faceTime=o->timestampSeconds;}
        std::stable_sort(e.body.begin(),e.body.end(),[](auto a,auto b){return a->timestampSeconds<b->timestampSeconds;});
        std::vector<const PersonDetection*> independent;
        for(auto o:e.body)if(independent.empty() || o->timestampSeconds-independent.back()->timestampSeconds>=.1-1e-9)
            independent.push_back(o);
        e.body.clear();
        constexpr std::size_t sampleLimit=64;
        for(std::size_t j=0;j<std::min(sampleLimit,independent.size());++j)
            e.body.push_back(independent[independent.size()<=sampleLimit ? j : j*(independent.size()-1)/(sampleLimit-1)]);
        for(auto o:e.body) {
            if(e.prototype.empty())e.prototype.resize(o->appearanceEmbedding.size());
            if(e.prototype.size()!=o->appearanceEmbedding.size())continue;
            for(std::size_t k=0;k<e.prototype.size();++k)e.prototype[k]+=o->appearanceEmbedding[k];
        }
    }
    const auto concurrent=[&](std::size_t a,std::size_t b) {
        return coVisibleDifferent(evidence[a].observations,evidence[b].observations);
    };
    struct Proposal {std::size_t source,anchor;double score;};
    std::vector<Proposal> proposals;
    for(std::size_t i=0;i<components.size();++i) {
        if(control.cancelled && control.cancelled())return;
        const auto& source=evidence[i];
        if(components[i].size()!=1 || source.faces || source.body.size()<3)continue;
        double best=-1,second=-1,covisibleCompetitor=-1;std::size_t bestIndex=components.size();
        for(std::size_t j=0;j<components.size();++j) {
            if((j%32)==0 && control.cancelled && control.cancelled())return;
            if(i==j || evidence[j].faces<3 || evidence[j].body.size()<3)continue;
            const double score=embeddingScore(source.prototype,evidence[j].prototype);
            // A different person visible in the same frame cannot be the
            // anchor, but their similar appearance still makes the source
            // descriptor ambiguous (for example when their boxes overlap).
            if(concurrent(i,j)) {covisibleCompetitor=std::max(covisibleCompetitor,score);continue;}
            if(score>best){second=best;best=score;bestIndex=j;}else second=std::max(second,score);
        }
        second=std::max(second,covisibleCompetitor);
        if(bestIndex==components.size() || second<0 || best<.82 || best-second<.08)continue;
        const auto& reference=evidence[bestIndex];std::size_t supported=0;
        for(auto sample:source.body) {
            std::size_t votes=0;
            for(auto anchor:reference.body)if(embeddingScore(sample->appearanceEmbedding,anchor->appearanceEmbedding)>=.78 && ++votes>=2)break;
            if(votes>=2)++supported;
        }
        if(supported>=3 && supported*5>=source.body.size()*3)proposals.push_back({i,bestIndex,best});
    }
    std::stable_sort(proposals.begin(),proposals.end(),[](const auto& a,const auto& b){return a.score>b.score;});
    std::vector<std::vector<std::size_t>> attached(components.size());
    for(const auto& proposal:proposals) {
        if(control.cancelled && control.cancelled())return;
        if(components[proposal.source].empty() || components[proposal.anchor].empty())continue;
        bool conflict=false;
        for(auto earlier:attached[proposal.anchor])if(concurrent(proposal.source,earlier))conflict=true;
        if(conflict)continue;
        auto& destination=components[proposal.anchor];auto& source=components[proposal.source];
        destination.insert(destination.end(),source.begin(),source.end());source.clear();
        attached[proposal.anchor].push_back(proposal.source);
    }
    // Split detections of an occluded person can overlap in time and have too
    // few visible joints for the ordinary duplicate check. Avoid a permanent
    // false separation when the smaller box, head/torso joints and body
    // descriptors all independently corroborate the same observed person.
    for(std::size_t i=0;i<components.size();++i) {
        if(control.cancelled && control.cancelled())return;
        if(components[i].size()!=1 || evidence[i].faces || evidence[i].observations.size()<2)continue;
        for(std::size_t j=0;j<i;++j) {
            if((j%32)==0 && control.cancelled && control.cancelled())return;
            if(components[j].size()!=1 || evidence[j].faces || evidence[j].observations.size()<2
                || embeddingScore(evidence[i].prototype,evidence[j].prototype)<.85)continue;
            bool same=false,contradiction=false;
            const auto& left=evidence[i].observations;const auto& right=evidence[j].observations;
            std::size_t a=0,b=0;
            while(a<left.size() && b<right.size()) {
                const auto ta=left[a]->timestampSeconds,tb=right[b]->timestampSeconds;
                if(std::abs(ta-tb)<=1e-6) {
                    std::size_t ae=a+1,be=b+1;
                    while(ae<left.size() && std::abs(left[ae]->timestampSeconds-ta)<=1e-6)++ae;
                    while(be<right.size() && std::abs(right[be]->timestampSeconds-tb)<=1e-6)++be;
                    for(auto p=a;p<ae;++p)for(auto q=b;q<be;++q) {
                        if(sameObservedSkeleton(*left[p],*right[q]) || samePartialObservedSkeleton(*left[p],*right[q]))same=true;
                        else contradiction=true;
                    }
                    a=ae;b=be;
                } else if(ta<tb)++a;else ++b;
            }
            if(same && !contradiction) {
                components[j].insert(components[j].end(),components[i].begin(),components[i].end());
                components[i].clear();break;
            }
        }
    }
    std::erase_if(components,[](const auto& c){return c.empty();});
}

// Recover short continuity across a cut when measured box/body agreement is
// corroborated by an identified companion or independently observed faces.
static void recoverCompanionContinuity(std::vector<std::vector<std::size_t>>& components,
    const std::vector<PersonTrack>& tracks,const std::vector<std::size_t>& indices,
    const DominantSelectionControl& control)
{
    struct Entry {std::size_t group;const PersonTrack* track;IdentitySummary identity;};
    std::vector<Entry> entries;
    std::vector<std::vector<const PersonDetection*>> observations(components.size());
    std::vector<std::size_t> faceSamples(components.size());
    std::vector<std::vector<double>> faceTimes(components.size());
    std::vector<std::vector<float>> facePrototypes(components.size());
    for(std::size_t group=0;group<components.size();++group)for(auto index:components[group]) {
        const auto& track=tracks[indices[index]];
        if(track.observations.size()<2 || !std::isfinite(track.observations.front().timestampSeconds)
            || !std::isfinite(track.observations.back().timestampSeconds))continue;
        auto summary=summarizeObservations(track.observations,true);
        entries.push_back({group,&track,std::move(summary)});
        for(const auto& o:track.observations)if(std::isfinite(o.timestampSeconds)) {
            observations[group].push_back(&o);
            if(embeddingScore(o.faceEmbedding,o.faceEmbedding)>.99) {
                faceTimes[group].push_back(o.timestampSeconds);
                auto& prototype=facePrototypes[group];
                if(prototype.empty())prototype.resize(o.faceEmbedding.size());
                if(prototype.size()==o.faceEmbedding.size())
                    for(std::size_t i=0;i<prototype.size();++i)prototype[i]+=o.faceEmbedding[i];
            }
        }
    }
    for(std::size_t group=0;group<faceTimes.size();++group) {
        auto& times=faceTimes[group];std::sort(times.begin(),times.end());
        double previous=-std::numeric_limits<double>::infinity();
        for(double time:times)if(time-previous>=.1-1e-9){++faceSamples[group];previous=time;}
    }
    for(auto& group:observations)std::stable_sort(group.begin(),group.end(),
        [](auto a,auto b){return a->timestampSeconds<b->timestampSeconds;});
    std::vector<std::size_t> byStart(entries.size());std::iota(byStart.begin(),byStart.end(),0);
    std::stable_sort(byStart.begin(),byStart.end(),[&](auto a,auto b){
        return entries[a].track->firstTimestampSeconds()<entries[b].track->firstTimestampSeconds();});
    struct Edge {std::size_t a,b;double score;};std::vector<Edge> edges;
    for(std::size_t i=0;i<entries.size();++i) {
        if(control.cancelled && control.cancelled())return;
        const auto& left=entries[i];const auto& last=left.track->observations.back();
        if(!std::isfinite(last.timestampSeconds) || left.identity.bodyEvidence<.45)continue;
        const auto begin=std::lower_bound(byStart.begin(),byStart.end(),last.timestampSeconds,
            [&](auto index,double time){return entries[index].track->firstTimestampSeconds()<time;});
        for(auto it=begin;it!=byStart.end();++it) {
            const auto& right=entries[*it];const auto& first=right.track->observations.front();
            const double gap=first.timestampSeconds-last.timestampSeconds;
            if(gap>.35)break;
            if(gap<=1e-6 || left.group==right.group || right.identity.bodyEvidence<.45)continue;
            const double box=last.box.iou(first.box),body=embeddingScore(left.identity.body,right.identity.body);
            if(box<.55 || body<.60)continue;
            if(left.identity.faceEvidence>=.45 && right.identity.faceEvidence>=.45
                && embeddingScore(left.identity.face,right.identity.face)<.363)continue;
            if(faceSamples[left.group]>=3 && faceSamples[right.group]>=3
                && embeddingScore(facePrototypes[left.group],facePrototypes[right.group])<.20)continue;
            if(coVisibleDifferent(observations[left.group],observations[right.group]))continue;
            bool companion=false;
            for(std::size_t k=0;k<observations.size() && !companion;++k) {
                if(k==left.group || k==right.group || faceSamples[k]<3)continue;
                const auto& group=observations[k];
                const auto findAt=[&](double time) {
                    return std::lower_bound(group.begin(),group.end(),time-1e-6,
                        [](auto item,double value){return item->timestampSeconds<value;});
                };
                const auto a=findAt(last.timestampSeconds),b=findAt(first.timestampSeconds);
                if(a==group.end() || b==group.end()
                    || std::abs((*a)->timestampSeconds-last.timestampSeconds)>1e-6
                    || std::abs((*b)->timestampSeconds-first.timestampSeconds)>1e-6)continue;
                const auto center=[](const BoundingBox& box){return (box.left+box.right)*.5;};
                const double before=center(last.box)-center((*a)->box);
                const double after=center(first.box)-center((*b)->box);
                if(before*after>0 && !sameObservedSkeleton(last,**a) && !sameObservedSkeleton(first,**b))companion=true;
            }
            // A cut can split one close-up even when no companion is in
            // frame. Require a well-observed face before the cut, an actual
            // face afterwards, and simultaneous position and body agreement.
            // The usual competing-edge and co-visibility checks still apply.
            const double boundaryFace=embeddingScore(left.identity.face,right.identity.face);
            const bool faceContinuity=faceSamples[left.group]>=3
                && left.identity.faceEvidence>=.99 && right.identity.faceEvidence>0
                && boundaryFace>=.52 && body>=.70 && box>=.65;
            if(companion || faceContinuity)edges.push_back({left.group,right.group,.65*box+.35*body});
        }
    }
    std::stable_sort(edges.begin(),edges.end(),[](const auto& a,const auto& b){return a.score>b.score;});
    std::vector<std::map<std::size_t,double>> alternatives(components.size());
    for(const auto& edge:edges) {
        auto& a=alternatives[edge.a];auto& b=alternatives[edge.b];
        a[edge.b]=std::max(a[edge.b],edge.score);b[edge.a]=std::max(b[edge.a],edge.score);
    }
    std::vector<std::size_t> owner(components.size());std::iota(owner.begin(),owner.end(),0);
    std::vector<std::vector<std::size_t>> seeds(components.size());
    for(std::size_t i=0;i<seeds.size();++i)seeds[i].push_back(i);
    for(const auto& edge:edges) {
        if(control.cancelled && control.cancelled())return;
        // A near-tie at either endpoint is a competing interpretation.
        const auto ambiguousFor=[&](std::size_t endpoint,std::size_t intended) {
            for(const auto& [other,score]:alternatives[endpoint])
                if(other!=intended && edge.score-score<.05)return true;
            return false;
        };
        const bool ambiguous=ambiguousFor(edge.a,edge.b) || ambiguousFor(edge.b,edge.a);
        if(ambiguous)continue;
        const auto a=owner[edge.a],b=owner[edge.b];if(a==b)continue;
        bool conflict=false;
        for(auto x:seeds[a])for(auto y:seeds[b])if(coVisibleDifferent(observations[x],observations[y]))conflict=true;
        if(conflict)continue;
        components[a].insert(components[a].end(),components[b].begin(),components[b].end());components[b].clear();
        for(auto seed:seeds[b]){owner[seed]=a;seeds[a].push_back(seed);}seeds[b].clear();
    }
    std::erase_if(components,[](const auto& c){return c.empty();});
}

// Detector duplicates may land in separate tracks, including after one of
// them has already joined a large identity component. Only merge when the
// same head and almost identical boxes recur in several simultaneous frames.
static void recoverOverlappingHeadDuplicates(std::vector<std::vector<std::size_t>>& components,
    const std::vector<PersonTrack>& tracks, const std::vector<std::size_t>& indices,
    const DominantSelectionControl& control)
{
    const auto sameHead = [](const PersonDetection& a, const PersonDetection& b) {
        if (a.box.iou(b.box) < .90) return false;
        const double scale = std::sqrt(std::min(a.box.area(),b.box.area()));
        if (!(scale > 0) || !std::isfinite(scale)) return false;
        double sum=0, maximum=0; std::size_t count=0;
        for (std::size_t i=0; i<std::min<std::size_t>({5,a.keypoints.size(),b.keypoints.size()}); ++i) {
            const auto& x=a.keypoints[i]; const auto& y=b.keypoints[i];
            if (!std::isfinite(x.confidence) || !std::isfinite(y.confidence)
                || x.confidence<.8 || y.confidence<.8 || !std::isfinite(x.x+x.y+y.x+y.y)) continue;
            const double distance=std::hypot(x.x-y.x,x.y-y.y)/scale;
            sum+=distance; maximum=std::max(maximum,distance); ++count;
        }
        return count>=3 && sum/count<=.012 && maximum<=.025;
    };
    for (std::size_t i=0; i<components.size(); ++i) {
        if (control.cancelled && control.cancelled()) return;
        if (components[i].size()!=1) continue;
        const auto& source=tracks[indices[components[i].front()]];
        if (source.observations.size()<3) continue;
        const auto sourceSummary=summarizeObservations(source.observations,true);
        std::size_t accepted=components.size(); bool ambiguous=false;
        for (std::size_t j=0; j<components.size(); ++j) {
            if (j==i || components[j].empty()) continue;
            if ((j%32)==0 && control.cancelled && control.cancelled()) return;
            bool matched=false, conflict=false;
            for (const auto member : components[j]) {
                const auto& target=tracks[indices[member]];
                if (target.observations.empty() || target.observations.back().timestampSeconds < source.observations.front().timestampSeconds
                    || target.observations.front().timestampSeconds > source.observations.back().timestampSeconds) continue;
                std::size_t a=0,b=0,count=0; double first=0,last=0;
                bool disagreement=false;
                while (a<source.observations.size() && b<target.observations.size()) {
                    const auto& x=source.observations[a]; const auto& y=target.observations[b];
                    if (std::abs(x.timestampSeconds-y.timestampSeconds)<=1e-6) {
                        if (!sameHead(x,y) || (!x.faceEmbedding.empty() && !y.faceEmbedding.empty()
                            && embeddingScore(x.faceEmbedding,y.faceEmbedding)<.363)) disagreement=true;
                        else { if (!count) first=x.timestampSeconds; last=x.timestampSeconds; ++count; }
                        ++a; ++b;
                    } else if (x.timestampSeconds<y.timestampSeconds) ++a; else ++b;
                }
                if (disagreement) { conflict=true; break; }
                if (count>=3 && last-first>=.3
                    && embeddingScore(sourceSummary.body,summarizeObservations(target.observations,true).body)>=.95)
                    matched=true;
            }
            if (!matched || conflict) continue;
            if (accepted!=components.size()) { ambiguous=true; break; }
            accepted=j;
        }
        if (!ambiguous && accepted!=components.size()) {
            components[accepted].push_back(components[i].front());
            components[i].clear();
        }
    }
    // A tiny duplicate may have only two frames and one valid body sample.
    // Recover it only when every frame coincides with the same precisely
    // localized head in one established track and several independent face
    // observations from that track corroborate its single face.
    for (std::size_t i=0;i<components.size();++i) {
        if (control.cancelled && control.cancelled()) return;
        if (components[i].size()!=1) continue;
        const auto& source=tracks[indices[components[i].front()]];
        if (source.observations.size()!=2
            || source.observations.back().timestampSeconds-source.observations.front().timestampSeconds<.15-1e-9) continue;
        const PersonDetection* sourceFace=nullptr; bool multipleFaces=false;
        for (const auto& observation:source.observations)
            if (embeddingScore(observation.faceEmbedding,observation.faceEmbedding)>.99) {
                if (sourceFace) multipleFaces=true; else sourceFace=&observation;
            }
        if (!sourceFace || multipleFaces) continue;
        std::vector<const PersonDetection*> sourceViews;
        for (const auto& o:source.observations) if(std::isfinite(o.timestampSeconds)) sourceViews.push_back(&o);
        if (sourceViews.size()!=source.observations.size()) continue;
        std::size_t accepted=components.size(); bool ambiguous=false;
        for (std::size_t j=0;j<components.size();++j) {
            if (control.cancelled && control.cancelled()) return;
            if (j==i || components[j].empty()) continue;
            bool matched=false, conflict=false;
            for (const auto member:components[j]) {
                const auto& target=tracks[indices[member]];
                if (target.observations.empty()
                    || target.observations.back().timestampSeconds < source.observations.front().timestampSeconds-1e-6
                    || target.observations.front().timestampSeconds > source.observations.back().timestampSeconds+1e-6) continue;
                std::vector<const PersonDetection*> targetViews;targetViews.reserve(target.observations.size());
                for (const auto& o:target.observations) if(std::isfinite(o.timestampSeconds)) targetViews.push_back(&o);
                if (coVisibleDifferent(sourceViews,targetViews)) { conflict=true;break; }
                bool completeOverlap=true;
                for (const auto* x:sourceViews) {
                    std::size_t matches=0;
                    for (const auto* y:targetViews)
                        if (std::abs(x->timestampSeconds-y->timestampSeconds)<=1e-6 && sameObservedHead(*x,*y)) ++matches;
                    if (matches!=1) {completeOverlap=false;break;}
                }
                if (!completeOverlap) continue;
                std::vector<double> scores;double previous=-INFINITY;
                for (const auto* view:targetViews) {
                    if (view->timestampSeconds-previous<.1-1e-9) continue;
                    const auto score=embeddingScore(sourceFace->faceEmbedding,view->faceEmbedding);
                    if (score>=0) {scores.push_back(score);previous=view->timestampSeconds;}
                }
                if (scores.size()<3) continue;
                std::sort(scores.begin(),scores.end(),std::greater<>());
                if (scores[0]>=.90 && scores[2]>=.80) matched=true;
            }
            if (!matched || conflict) continue;
            if (accepted!=components.size()) {ambiguous=true;break;}
            accepted=j;
        }
        if (!ambiguous && accepted!=components.size()) {
            components[accepted].push_back(components[i].front());components[i].clear();
        }
    }
    std::erase_if(components,[](const auto& c){return c.empty();});
}

std::vector<std::size_t> characterSelectionOrder(const std::vector<CharacterCandidate>& candidates,
    std::size_t maximumUnresolved,std::optional<std::size_t> suggestedId)
{
    std::vector<std::size_t> order(candidates.size());std::iota(order.begin(),order.end(),0);
    const auto finite=[](double x) {return std::isfinite(x) ? x : 0.;};
    std::stable_sort(order.begin(),order.end(),[&](auto a,auto b) {
        const auto& x=candidates[a];const auto& y=candidates[b];
        if(x.distinctIdentity!=y.distinctIdentity)return x.distinctIdentity;
        const bool sx=suggestedId && x.id==*suggestedId,sy=suggestedId && y.id==*suggestedId;
        if(sx!=sy)return sx;
        if(finite(x.presenceSeconds)!=finite(y.presenceSeconds))return finite(x.presenceSeconds)>finite(y.presenceSeconds);
        if(finite(x.evidenceQuality)!=finite(y.evidenceQuality))return finite(x.evidenceQuality)>finite(y.evidenceQuality);
        return x.id<y.id;
    });
    std::size_t unresolved=0;
    std::erase_if(order,[&](auto index){return !candidates[index].distinctIdentity && unresolved++>=maximumUnresolved;});
    return order;
}

std::vector<CharacterCandidate> characterCandidates(const std::vector<PersonTrack>& tracks,
    const DominantSelectionControl& control)
{
    std::vector<IdentitySummary> summaries;
    std::vector<std::size_t> indices;
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        if (control.cancelled && control.cancelled()) return {};
        if (tracks[i].observations.size() < 2) continue;
        summaries.push_back(summarizeObservations(tracks[i].observations,true));
        indices.push_back(i);
    }
    std::vector<std::vector<std::size_t>> components;
    // Candidate selection requires stronger seed links than legacy dominant
    // tracking: a weak mean or similar clothing alone previously joined
    // different people before their face views could be compared. The
    // following recovery passes reconsider lower-scoring matches using
    // repeated observations and conflict checks.
    selectIdentityComponents(summaries, control, &components,.60,.90);
    recoverCharacterViews(components,tracks,indices,control);
    if(control.cancelled && control.cancelled())return {};
    recoverUniqueBodyTracks(components,tracks,indices,control);
    if(control.cancelled && control.cancelled())return {};
    recoverCompanionContinuity(components,tracks,indices,control);
    if(control.cancelled && control.cancelled())return {};
    // Recheck newly assembled galleries without losing contradictions between
    // their original face-backed tracks.
    recoverCharacterViews(components,tracks,indices,control);
    recoverOverlappingHeadDuplicates(components,tracks,indices,control);
    std::vector<CharacterCandidate> result;
    const auto addPrototype = [](std::vector<float>& sum, const std::vector<float>& value) {
        double norm = 0;
        for (float v : value) { if (!std::isfinite(v)) return; norm += double(v) * v; }
        if (norm <= 1e-12 || (!sum.empty() && sum.size() != value.size())) return;
        if (sum.empty()) sum.resize(value.size());
        for (std::size_t j = 0; j < sum.size(); ++j) sum[j] += value[j] / std::sqrt(norm);
    };
    const auto appearanceBox = [](const PersonDetection& observation) {
        if (observation.frameWidth<=0 || observation.frameHeight<=0) return BoundingBox{};
        return BoundingBox{observation.box.left/observation.frameWidth,observation.box.top/observation.frameHeight,
            observation.box.right/observation.frameWidth,observation.box.bottom/observation.frameHeight};
    };
    const auto appearanceQuality = [](const PersonDetection& observation) {
        const double pose=std::isfinite(observation.keypointConfidence)
            ? std::clamp(observation.keypointConfidence,0.,1.) : 0.;
        // Prefer an actually measured face over a well-observed back/torso
        // when explaining which person this candidate represents.
        return embeddingScore(observation.faceEmbedding,observation.faceEmbedding)>.99
            ? .5+.5*pose : .5*pose;
    };
    for (const auto& component : components) {
        if (control.cancelled && control.cancelled()) return {};
        CharacterCandidate candidate;
        candidate.id = std::numeric_limits<std::size_t>::max();
        std::vector<CharacterAppearance> appearances;
        std::vector<std::pair<double,double>> intervals;
        double quality = 0;
        for (auto index : component) {
            const auto& track = tracks[indices[index]];
            const auto& summary = summaries[index];
            candidate.id = std::min(candidate.id, track.id);
            candidate.trackIds.push_back(track.id);
            candidate.observationCount += track.observations.size();
            addPrototype(candidate.prototype.body, summary.body);
            addPrototype(candidate.prototype.face, summary.face);
            candidate.prototype.bodyEvidence = std::max(candidate.prototype.bodyEvidence, summary.bodyEvidence);
            candidate.prototype.faceEvidence = std::max(candidate.prototype.faceEvidence, summary.faceEvidence);
            // Presence is a union of observed intervals, never a sliding-window count.
            for (const auto& observation : track.observations) {
                const double t = observation.timestampSeconds;
                if (!std::isfinite(t)) continue;
                const double duration = std::isfinite(observation.frameDurationSeconds)
                    ? std::max(0.0, observation.frameDurationSeconds) : 0.0;
                intervals.emplace_back(t, t + duration);
                const double q = std::isfinite(observation.keypointConfidence)
                    ? std::clamp(observation.keypointConfidence, 0.0, 1.0) : 0.0;
                quality += q;
            }
            // Choose one good observed frame per track, then distribute previews
            // over the whole appearance history instead of choosing adjacent crops.
            const auto best = std::max_element(track.observations.begin(), track.observations.end(),
                [&](const auto& a, const auto& b) { return appearanceQuality(a) < appearanceQuality(b); });
            appearances.push_back({track.id, best->timestampSeconds, appearanceQuality(*best), appearanceBox(*best)});
        }
        std::sort(intervals.begin(), intervals.end());
        double end = -std::numeric_limits<double>::infinity();
        for (const auto& [first,last] : intervals) {
            candidate.presenceSeconds += std::max(0.0, last - std::max(first,end));
            end = std::max(end,last);
        }
        candidate.prototype.duration = candidate.presenceSeconds;
        candidate.evidenceQuality = candidate.observationCount ? quality / candidate.observationCount : 0;
        std::sort(candidate.trackIds.begin(), candidate.trackIds.end());
        std::sort(appearances.begin(), appearances.end(), [](const auto& a, const auto& b) {
            return a.timestampSeconds < b.timestampSeconds;
        });
        for (std::size_t i = 0; i < std::min<std::size_t>(5, appearances.size()); ++i) {
            const auto index = appearances.size() <= 5 ? i : i * (appearances.size()-1) / 4;
            candidate.appearances.push_back(appearances[index]);
        }
        // A single long track still needs several actual appearances for inspection.
        if (component.size() == 1) {
            const auto& track = tracks[indices[component.front()]];
            candidate.appearances.clear();
            const auto n = std::min<std::size_t>(5, track.observations.size());
            for (std::size_t i = 0; i < n; ++i) {
                const auto& observation = track.observations[i * (track.observations.size()-1) / (n-1)];
                candidate.appearances.push_back({track.id, observation.timestampSeconds,
                    appearanceQuality(observation), appearanceBox(observation)});
            }
        }
        // Temporal sampling must not accidentally discard the clearest face.
        const auto best=std::max_element(appearances.begin(),appearances.end(),
            [](const auto& a,const auto& b){return a.quality<b.quality;});
        if(best!=appearances.end() && std::none_of(candidate.appearances.begin(),candidate.appearances.end(),
            [&](const auto& a){return a.trackId==best->trackId && a.timestampSeconds==best->timestampSeconds;})) {
            *std::min_element(candidate.appearances.begin(),candidate.appearances.end(),
                [](const auto& a,const auto& b){return a.quality<b.quality;})=*best;
            std::sort(candidate.appearances.begin(),candidate.appearances.end(),
                [](const auto& a,const auto& b){return a.timestampSeconds<b.timestampSeconds;});
        }
        result.push_back(std::move(candidate));
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.id < b.id; });
    // A disconnected identity component is not evidence of a new person.
    // Establish distinctness independently; never merge ambiguous components.
    std::map<std::size_t,std::size_t> owner;
    for (std::size_t i=0;i<result.size();++i)
        for (auto id:result[i].trackIds) owner[id]=i;
    std::map<double,std::map<std::size_t,const PersonDetection*>> frames;
    for (const auto& track:tracks) {
        const auto found=owner.find(track.id);
        if (found==owner.end()) continue;
        for (const auto& observation:track.observations)
            if (std::isfinite(observation.timestampSeconds))
                frames[observation.timestampSeconds].try_emplace(found->second,&observation);
    }
    struct Separation { int count=0; double first=0,last=0; };
    std::map<std::pair<std::size_t,std::size_t>,Separation> separation;
    for (const auto& [time,people]:frames) {
        if (control.cancelled && control.cancelled()) return {};
        for (auto a=people.begin();a!=people.end();++a) for (auto b=std::next(a);b!=people.end();++b) {
            const auto& x=*a->second; const auto& y=*b->second;
            if (x.box.area()<=0 || y.box.area()<=0) continue;
            bool separate=x.keypointConfidence>=.6 && y.keypointConfidence>=.6
                && (x.box.right<y.box.left || y.box.right<x.box.left);
            if (!separate && !x.keypoints.empty() && !y.keypoints.empty()) {
                const auto& nx=x.keypoints.front(); const auto& ny=y.keypoints.front();
                const double headScale=.4*std::min(x.box.right-x.box.left,y.box.right-y.box.left);
                separate=nx.confidence>=.7 && ny.confidence>=.7 && headScale>0
                    && std::hypot(nx.x-ny.x,nx.y-ny.y)>headScale;
            }
            if (!separate) continue;
            auto& evidence=separation[{a->first,b->first}];
            if (!evidence.count++) evidence.first=time;
            evidence.last=time;
        }
    }
    std::vector<std::size_t> order(result.size()),confirmed;
    std::iota(order.begin(),order.end(),0);
    std::stable_sort(order.begin(),order.end(),[&](auto a,auto b){return result[a].presenceSeconds>result[b].presenceSeconds;});
    for (auto i:order) {
        auto& candidate=result[i];
        if (candidate.prototype.faceEvidence<.45
            && !(candidate.trackIds.size()>1 && candidate.prototype.bodyEvidence>=.45)) continue;
        const bool distinct=std::all_of(confirmed.begin(),confirmed.end(),[&](auto other){
            const auto pair=std::minmax(i,other);
            const auto found=separation.find({pair.first,pair.second});
            if (found!=separation.end() && found->second.count>=3
                && found->second.last-found->second.first>=.25) return true;
            const auto& face=result[other].prototype;
            return candidate.prototype.faceEvidence>=.65 && face.faceEvidence>=.65
                && !candidate.prototype.face.empty() && !face.face.empty()
                && candidate.prototype.face.size()==face.face.size()
                && embeddingScore(candidate.prototype.face,face.face)<.20;
        });
        if (distinct) {candidate.distinctIdentity=true;confirmed.push_back(i);}
    }
    return result;
}

TargetSuggestion suggestTargetCharacter(const CharacterCandidate& target,
    const std::vector<CharacterCandidate>& candidates)
{
    TargetSuggestion result;
    if (target.prototype.faceEvidence < .45 || embeddingScore(target.prototype.face,target.prototype.face) < .99)
        return result;
    double best = -1, second = -1;
    for (const auto& candidate : candidates) {
        if (candidate.prototype.faceEvidence < .45) continue;
        const double score = embeddingScore(target.prototype.face,candidate.prototype.face);
        if (score > best) {
            second = best; best = score; result.candidateId = candidate.id;
        } else second = std::max(second,score);
    }
    result.similarity = std::max(0.0,best);
    result.margin = best - std::max(0.0,second);
    if (!result.candidateId) return result;
    result.automatic = best >= .65 && result.margin >= .12;
    result.reason = result.automatic ? TargetSuggestionReason::StrongFaceAgreement
        : best >= .363 ? TargetSuggestionReason::Ambiguous : TargetSuggestionReason::NoMatch;
    return result;
}

DominantVideoSelection selectDominantVideoWindows(const std::vector<MotionWindow>& windows,
    const DominantVideoSelectionControl& control)
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
    result.recoveredWindows.assign(windows.size(),false);
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
    if(control.auditShotRecovery && groups[best].size()>=2) {
        // Freeze references before recovery. Recovered shots must never become
        // new anchors, so a sequence of weaker links cannot change the target.
        std::set<std::pair<std::string,std::size_t>> recoveredShots;
        for(std::size_t source=0;source<sources.size();++source) {
            if(owner[source]==best)continue;
            const auto& candidate=*sources[source];
            if(std::any_of(groups[best].begin(),groups[best].end(),[&](auto reference) {
                return embeddingScore(candidate.face,sources[reference]->face)<.363;
            }))continue;
            for(const auto& [shot,window]:candidate.shots) {
                if(window->faceConfidence<.45 || embeddingScore(window->faceEmbedding,window->faceEmbedding)<=.99)continue;
                bool supported=false;
                for(const auto reference:groups[best]) {
                    const auto& lead=*sources[reference];
                    // Clear aggregate contradiction remains a veto. A mask or
                    // view can weaken the mean, but cannot establish identity.
                    if(embeddingScore(candidate.face,lead.face)<.363)continue;
                    std::vector<const std::vector<float>*> votes;
                    for(const auto& anchor:lead.anchors)
                        if(embeddingScore(window->faceEmbedding,anchor)>=.55)votes.push_back(&anchor);
                    for(std::size_t a=0;a<votes.size() && !supported;++a)
                        for(std::size_t b=a+1;b<votes.size();++b)
                            if(embeddingScore(*votes[a],*votes[b])>=.50) {supported=true;break;}
                    if(supported)break;
                }
                if(supported)recoveredShots.emplace(names[source],shot);
            }
        }
        for(std::size_t i=0;i<windows.size();++i) {
            const auto& window=windows[i];
            if(!result.windows[i] && window.hasSceneIndex && recoveredShots.contains({window.sourceId,window.sceneIndex})
                && window.faceConfidence>=.45 && embeddingScore(window.faceEmbedding,window.faceEmbedding)>.99) {
                // A scene can contain more than one track. Do not propagate a
                // selected track's identity to an unrelated co-visible actor.
                const auto& representative=evidence.at(window.sourceId).shots.at(window.sceneIndex);
                if(window.trackId!=representative->trackId || embeddingScore(window.faceEmbedding,representative->faceEmbedding)<.70)continue;
                result.windows[i]=true;result.recoveredWindows[i]=true;
            }
        }
        for(const auto& [source,shot]:recoveredShots)
            if(!std::binary_search(result.sources.begin(),result.sources.end(),source)) {
                result.sources.push_back(source);std::sort(result.sources.begin(),result.sources.end());
            }
    }
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
    const std::vector<PersonTrack>* rc16Tracks,const DominantSelectionControl& control)
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
    auto original = selectDominantIdentities(summaries,control);
    if(control.cancelled && control.cancelled())return result;
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
        const auto previous = selectDominantIdentities(previousSummaries,control);
        if(control.cancelled && control.cancelled())return result;
        for (std::size_t i = 0; i < previous.size(); ++i) if (previous[i])
            for (const auto& observation : (*rc16Tracks)[previousIndices[i]].observations)
                previouslyAdmitted.insert(key(observation));
        for (std::size_t i = 0; i < original.size(); ++i) if (original[i]) {
            const auto& observations = tracks[indices[i]].observations;
            original[i] = std::any_of(observations.begin(), observations.end(),
                [&](const auto& observation) { return previouslyAdmitted.contains(key(observation)); });
        }
    }
    std::vector<bool> observedFace(summaries.size()), readyFace(summaries.size()), readyBody(summaries.size());
    for (std::size_t i=0;i<summaries.size();++i) {
        observedFace[i]=summaries[i].faceEvidence>0
            && embeddingScore(summaries[i].face,summaries[i].face)>.99;
        readyFace[i]=observedFace[i] && summaries[i].faceEvidence>=.45;
        readyBody[i]=summaries[i].bodyEvidence>=.45
            && embeddingScore(summaries[i].body,summaries[i].body)>.99;
    }
    const auto faceObserved=[&](std::size_t i) {return observedFace[i];};
    const auto faceReady=[&](std::size_t i) {return readyFace[i];};
    const auto bodyReady=[&](std::size_t i) {return readyBody[i];};
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
    if(control.progress)control.progress(DominantSelectionStage::Recovery,0,summaries.size());
    for (std::size_t i = 0; i < summaries.size(); ++i) {
        if(control.cancelled && control.cancelled())return {};
        if(control.progress)control.progress(DominantSelectionStage::Recovery,i,summaries.size());
        if (original[i] || !faceReady(i)
            || (!bodyReady(i) && !control.auditFaceConsensusRecovery)) continue;
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
        const bool facialConsensus = control.auditFaceConsensusRecovery
            && anchors.size() >= 3 && votes >= 2 && votes * 3 >= anchors.size() * 2;
        candidate[i] = votes >= 2 && votes > anchors.size() / 2 && (body || facialConsensus);
    }
    for (std::size_t i = 0; i < candidate.size(); ++i) if (candidate[i])
        for (std::size_t j = i + 1; j < candidate.size(); ++j) if (candidate[j] && coVisible(i, j))
            ambiguous[i] = ambiguous[j] = true;
    const bool trace = std::getenv("PF_DEBUG_DOMINANT") != nullptr;
    for (std::size_t i = 0; i < candidate.size(); ++i) {
        if(control.cancelled && control.cancelled())return {};
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
                if (control.auditFaceConsensusRecovery
                    || (run.bodyEvidence >= .45 && embeddingScore(run.body, run.body) > .99)) {
                    for (const auto anchor : anchors) {
                        if (embeddingScore(run.face, summaries[anchor].face) < .363) continue;
                        ++votes;
                        body = body || (bodyReady(anchor)
                            && embeddingScore(run.body, summaries[anchor].body) >= .76);
                    }
                }
                const bool facialConsensus = control.auditFaceConsensusRecovery
                    && independentFaces >= 3 && anchors.size() >= 3
                    && votes >= 2 && votes * 3 >= anchors.size() * 2;
                if (votes >= 2 && votes > anchors.size() / 2 && (body || facialConsensus))
                    ranges.push_back({first, last + 1});
            }
            independentFaces = 0;
        };
        // Same maximum gap as default geometric tracking. Bound interpolation
        // between identity observations; never extrapolate before/after them.
        constexpr double maximumIdentityGapSeconds = 1.0;
        for (std::size_t j = 0; j < observations.size(); ++j) {
            if(control.cancelled && control.cancelled())return {};
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
    if (control.auditDirectFaceRecovery) {
        // Direct multi-image support from one admitted view can be stronger
        // than votes against averages from many poorly lit views. Never grow
        // a run outside actual face observations or across an identity gap.
        std::vector<std::vector<TrackObservationRange>> proposals(tracks.size());
        for (std::size_t i=0;i<summaries.size();++i) {
            if(control.cancelled && control.cancelled())return {};
            const auto index=indices[i];
            if(result.tracks[index] || std::any_of(lead.begin(),lead.end(),
                [&](std::size_t anchor){return coVisible(i,anchor);}))continue;
            const auto& observations=tracks[index].observations;
            std::vector<DirectFaceSample> samples;
            std::size_t first=0,last=0;
            const auto finishDirect=[&] {
                if(samples.size()<3)return;
                for(const auto anchor:anchors) {
                    if(control.cancelled && control.cancelled())return;
                    std::vector<DirectFaceSample> reference;
                    const auto anchorIndex=indices[anchor];
                    const auto& anchorObservations=tracks[anchorIndex].observations;
                    for(const auto range:result.observationRuns[anchorIndex])
                        for(std::size_t n=range.begin;n<range.end;++n) {
                            const auto& observation=anchorObservations[n];
                            if(!observation.faceEmbedding.empty())
                                reference.push_back({observation.timestampSeconds,observation.faceEmbedding});
                        }
                    if(reference.size()<3)continue;
                    const auto consensus=compareDirectFaces(samples,samples.front().timestampSeconds,
                        samples.back().timestampSeconds,reference,reference.front().timestampSeconds,
                        reference.back().timestampSeconds);
                    if(consensus.decision==FaceConsensusDecision::Supported) {
                        proposals[index].push_back({first,last+1});break;
                    }
                }
            };
            for(std::size_t j=0;j<observations.size();++j) {
                if(control.cancelled && control.cancelled())return {};
                const auto& observation=observations[j];
                if(observation.faceEmbedding.empty())continue;
                if(!samples.empty() && (observation.timestampSeconds-samples.back().timestampSeconds>1.0
                    || observation.timestampSeconds<=samples.back().timestampSeconds)) {
                    finishDirect();samples.clear();
                }
                if(samples.empty())first=j;
                last=j;samples.push_back({observation.timestampSeconds,observation.faceEmbedding});
            }
            finishDirect();
        }
        // Competing co-visible candidates cannot both become the source lead.
        for(std::size_t i=0;i<summaries.size();++i) {
            if(control.cancelled && control.cancelled())return {};
            const auto index=indices[i];if(proposals[index].empty())continue;
            bool competing=false;
            for(std::size_t j=0;j<summaries.size();++j)
                if(i!=j && !proposals[indices[j]].empty() && coVisible(i,j)) {competing=true;break;}
            if(!competing) {
                result.observationRuns[index]=proposals[index];
                result.tracks[index]=result.recovered[index]=true;
            }
        }
    }
    if(control.progress)control.progress(DominantSelectionStage::Recovery,summaries.size(),summaries.size());
    return result;
}

} // namespace pfcore
