#include "pfcore/MotionMatcher.hpp"
#include "pfcore/MotionIndex.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <unordered_set>

namespace pfcore {
namespace {

using Descriptor = std::vector<double>;

using NormalizedPose = std::vector<std::pair<double, double>>;

std::vector<NormalizedPose> normalizePoses(const MotionWindow& window,
                                           bool normalizeSize)
{
    std::vector<NormalizedPose> normalized;
    normalized.reserve(window.frames.size());
    for (const PoseFrame& frame : window.frames) {
        if (frame.keypoints.empty()) {
            normalized.emplace_back();
            continue;
        }
        double cx = 0.0, cy = 0.0, weight = 0.0;
        for (const auto& point : frame.keypoints) {
            const double w = std::max(0.0, point.confidence);
            cx += point.x * w;
            cy += point.y * w;
            weight += w;
        }
        if (weight <= 1e-9) weight = static_cast<double>(frame.keypoints.size());
        cx /= weight;
        cy /= weight;
        double scale = 1.0;
        if (normalizeSize) {
            scale = 0.0;
            for (const auto& point : frame.keypoints)
                scale = std::max(scale, std::hypot(point.x - cx, point.y - cy));
            if (scale <= 1e-9) scale = 1.0;
        }
        NormalizedPose pose;
        pose.reserve(frame.keypoints.size());
        for (const auto& point : frame.keypoints)
            pose.emplace_back((point.x - cx) / scale, (point.y - cy) / scale);
        normalized.push_back(std::move(pose));
    }
    return normalized;
}

double motionDelta(const MotionWindow& window, const std::vector<NormalizedPose>& poses)
{
    if (poses.size() < 2) return 0.0;
    double total = 0.0;
    std::size_t transitions = 0;
    for (std::size_t frame = 1; frame < poses.size(); ++frame) {
        const auto& previous = poses[frame - 1];
        const auto& current = poses[frame];
        if (previous.empty() || current.empty() || previous.size() != current.size()) continue;
        double delta = 0.0;
        std::size_t valid = 0;
        for (std::size_t point = 0; point < current.size(); ++point) {
            if (window.frames[frame - 1].keypoints[point].confidence <= 0.0
                || window.frames[frame].keypoints[point].confidence <= 0.0) {
                continue;
            }
            delta += std::hypot(current[point].first - previous[point].first,
                                current[point].second - previous[point].second);
            ++valid;
        }
        if (valid == 0) continue;
        // Normalize the requested sum by the number of observed joints.  This
        // is the same delta-K signal, without making the threshold model-size
        // dependent.
        total += delta / static_cast<double>(valid);
        ++transitions;
    }
    return transitions == 0 ? 0.0 : total / static_cast<double>(transitions);
}

std::vector<Descriptor> describe(const std::vector<NormalizedPose>& normalized,
                                 const MotionWindow& window)
{
    std::vector<Descriptor> result;
    result.reserve(normalized.size());
    for (std::size_t frameIndex = 0; frameIndex < normalized.size(); ++frameIndex) {
        const auto& pose = normalized[frameIndex];
        if (pose.empty()) { result.emplace_back(); continue; }
        const auto* previous = frameIndex > 0 ? &normalized[frameIndex - 1] : nullptr;
        const double dt = frameIndex > 0
            ? std::max(1e-3, window.frames[frameIndex].timestampSeconds
                - window.frames[frameIndex - 1].timestampSeconds)
            : 1.0;
        Descriptor descriptor;
        descriptor.reserve(pose.size() * 4);
        for (std::size_t pointIndex = 0; pointIndex < pose.size(); ++pointIndex) {
            descriptor.push_back(pose[pointIndex].first);
            descriptor.push_back(pose[pointIndex].second);
            double velocityX = 0.0;
            double velocityY = 0.0;
            if (previous && previous->size() == pose.size()) {
                velocityX = (pose[pointIndex].first - (*previous)[pointIndex].first) / dt;
                velocityY = (pose[pointIndex].second - (*previous)[pointIndex].second) / dt;
            }
            descriptor.push_back(std::clamp(velocityX, -4.0, 4.0));
            descriptor.push_back(std::clamp(velocityY, -4.0, 4.0));
        }
        result.push_back(std::move(descriptor));
    }
    return result;
}

double frameDistance(const Descriptor& left, const Descriptor& right)
{
    if (left.empty() || right.empty() || left.size() != right.size()) return 1.0;
    double sum = 0.0;
    for (std::size_t i = 0; i < left.size(); ++i) sum += std::abs(left[i] - right[i]);
    return sum / static_cast<double>(left.size());
}

double noiseFloor(const std::vector<Descriptor>& descriptors)
{
    if (descriptors.size() < 2) return 0.0;
    std::vector<double> deltas;
    deltas.reserve(descriptors.size() - 1);
    for (std::size_t i = 1; i < descriptors.size(); ++i) {
        if (descriptors[i - 1].empty() || descriptors[i].empty()
            || descriptors[i - 1].size() != descriptors[i].size()) {
            continue;
        }
        deltas.push_back(frameDistance(descriptors[i - 1], descriptors[i]));
    }
    if (deltas.empty()) return 0.0;
    const auto middle = deltas.begin() + static_cast<std::ptrdiff_t>(deltas.size() / 2);
    std::nth_element(deltas.begin(), middle, deltas.end());
    return *middle;
}

double coarseSimilarity(const std::vector<Descriptor>& left,
                        const std::vector<Descriptor>& right)
{
    if (left.empty() || right.empty()) return 0.0;
    const std::size_t samples = std::min<std::size_t>(4, std::min(left.size(), right.size()));
    if (samples == 0) return 0.0;
    double distance = 0.0;
    std::size_t used = 0;
    for (std::size_t i = 0; i < samples; ++i) {
        const std::size_t li = (i * (left.size() - 1)) / std::max<std::size_t>(1, samples - 1);
        const std::size_t ri = (i * (right.size() - 1)) / std::max<std::size_t>(1, samples - 1);
        const double cost = frameDistance(left[li], right[ri]);
        if (cost < 1.0 || (!left[li].empty() && !right[ri].empty())) {
            distance += cost;
            ++used;
        }
    }
    return used == 0 ? 0.0 : std::clamp(std::exp(-(distance / static_cast<double>(used))), 0.0, 1.0);
}

double cosineSimilarity(const Descriptor& left, const Descriptor& right)
{
    if (left.empty() || right.empty() || left.size() != right.size()) return 0.0;
    double dot = 0.0;
    double leftNorm = 0.0;
    double rightNorm = 0.0;
    for (std::size_t i = 0; i < left.size(); ++i) {
        dot += left[i] * right[i];
        leftNorm += left[i] * left[i];
        rightNorm += right[i] * right[i];
    }
    const double denominator = std::sqrt(leftNorm * rightNorm);
    if (denominator <= 1e-12) return 0.0;
    return std::clamp(dot / denominator, -1.0, 1.0);
}

double shapeSimilarity(const std::vector<NormalizedPose>& left,
                       const std::vector<NormalizedPose>& right)
{
    if (left.empty() || right.empty()) return 0.0;
    const std::size_t samples = std::min<std::size_t>(8, std::min(left.size(), right.size()));
    double score = 0.0;
    std::size_t used = 0;
    for (std::size_t sample = 0; sample < samples; ++sample) {
        const std::size_t li = (sample * (left.size() - 1))
            / std::max<std::size_t>(1, samples - 1);
        const std::size_t ri = (sample * (right.size() - 1))
            / std::max<std::size_t>(1, samples - 1);
        const auto& a = left[li];
        const auto& b = right[ri];
        if (a.empty() || b.empty()) continue;

        // A pose model's joint order is its topology contract.  A different
        // number of visible joints is therefore a real penalty, not missing
        // noise.  Pairwise distances add an anatomy/proportion check without
        // hardcoding a particular COCO or custom skeleton graph.
        const double topology = static_cast<double>(std::min(a.size(), b.size()))
            / static_cast<double>(std::max(a.size(), b.size()));
        const std::size_t points = std::min(a.size(), b.size());
        double error = 0.0;
        std::size_t pairs = 0;
        for (std::size_t i = 0; i < points; ++i) {
            for (std::size_t j = i + 1; j < points; ++j) {
                const double leftDistance = std::hypot(a[i].first - a[j].first,
                                                       a[i].second - a[j].second);
                const double rightDistance = std::hypot(b[i].first - b[j].first,
                                                        b[i].second - b[j].second);
                error += std::abs(leftDistance - rightDistance);
                ++pairs;
            }
        }
        const double proportionScore = pairs == 0
            ? 1.0
            : std::exp(-3.0 * error / static_cast<double>(pairs));
        score += topology * proportionScore;
        ++used;
    }
    return used == 0 ? 0.0 : std::clamp(score / static_cast<double>(used), 0.0, 1.0);
}

double temporalCosineScore(const std::vector<Descriptor>& left,
                           const std::vector<Descriptor>& right)
{
    if (left.empty() || right.empty()) return 0.0;
    const std::size_t samples = std::min(left.size(), right.size());
    double score = 0.0;
    std::size_t used = 0;
    for (std::size_t sample = 0; sample < samples; ++sample) {
        const std::size_t li = (sample * (left.size() - 1))
            / std::max<std::size_t>(1, samples - 1);
        const std::size_t ri = (sample * (right.size() - 1))
            / std::max<std::size_t>(1, samples - 1);
        const double cosine = cosineSimilarity(left[li], right[ri]);
        if (cosine <= 0.0) continue;
        score += cosine;
        ++used;
    }
    return used == 0 ? 0.0 : std::clamp(score / static_cast<double>(used), 0.0, 1.0);
}

double duration(const MotionWindow& window);

bool hasTemporalSupport(const MotionWindow& window,
                        const MotionMatcherParams& params)
{
    if (window.frames.size() < 3) return false;
    const double span = duration(window);
    return window.frames.size() >= params.minTemporalFrames
        || (span + 1e-9 >= params.minTemporalDurationSec && window.frames.size() >= 3);
}

bool hasTemporalRun(const std::vector<Descriptor>& left,
                   const std::vector<Descriptor>& right,
                   const MotionWindow& leftWindow,
                   const MotionWindow& rightWindow,
                   const MotionMatcherParams& params)
{
    if (left.empty() || right.empty()) return false;
    const std::size_t samples = std::min(left.size(), right.size());
    if (samples < 3) return false;
    std::size_t run = 0;
    std::size_t bestRun = 0;
    std::size_t runStartSample = 0;
    std::size_t bestStartSample = 0;
    std::size_t bestEndSample = 0;
    for (std::size_t sample = 0; sample < samples; ++sample) {
        const std::size_t leftIndex = (sample * (left.size() - 1))
            / std::max<std::size_t>(1, samples - 1);
        const std::size_t rightIndex = (sample * (right.size() - 1))
            / std::max<std::size_t>(1, samples - 1);
        if (cosineSimilarity(left[leftIndex], right[rightIndex])
            >= params.temporalSimilarityThreshold) {
            if (run == 0) runStartSample = sample;
            ++run;
            if (run > bestRun) {
                bestRun = run;
                bestStartSample = runStartSample;
                bestEndSample = sample;
            }
        } else {
            run = 0;
        }
    }
    const auto sampleTimestamp = [](const MotionWindow& window, std::size_t sample,
                                    std::size_t sampleCount) {
        const std::size_t index = (sample * (window.frames.size() - 1))
            / std::max<std::size_t>(1, sampleCount - 1);
        return window.frames[index].timestampSeconds;
    };
    const double runDuration = std::min(
        sampleTimestamp(leftWindow, bestEndSample, samples)
            - sampleTimestamp(leftWindow, bestStartSample, samples),
        sampleTimestamp(rightWindow, bestEndSample, samples)
            - sampleTimestamp(rightWindow, bestStartSample, samples));
    const bool enoughFrames = bestRun >= params.minTemporalFrames
        || (bestRun >= 3
            && runDuration + 1e-9 >= params.minTemporalDurationSec);
    return enoughFrames;
}

double duration(const MotionWindow& window)
{
    if (window.frames.size() < 2) return 0.0;
    return std::max(0.0, window.frames.back().timestampSeconds - window.frames.front().timestampSeconds);
}

std::vector<double> embedding(const std::vector<Descriptor>& descriptors,
                              std::size_t dimension)
{
    constexpr std::size_t frameSamples = 8;
    std::vector<double> result(frameSamples * dimension, 0.0);
    if (descriptors.empty() || dimension == 0) return result;
    for (std::size_t sample = 0; sample < frameSamples; ++sample) {
        const std::size_t frame = (sample * (descriptors.size() - 1))
            / std::max<std::size_t>(1, frameSamples - 1);
        const auto& descriptor = descriptors[frame];
        const std::size_t copyCount = std::min(dimension, descriptor.size());
        std::copy_n(descriptor.begin(), copyCount, result.begin() + sample * dimension);
    }
    return result;
}

struct PreparedWindow {
    std::vector<NormalizedPose> poses;
    std::vector<Descriptor> descriptors;
    std::vector<double> embedding;
    double motionDelta = 0.0;
};

MotionMatch comparePrepared(const MotionWindow& left, const MotionWindow& right,
                            const PreparedWindow& leftPrepared,
                            const PreparedWindow& rightPrepared,
                            const MotionMatcherParams& params,
                            std::size_t leftIndex, std::size_t rightIndex)
{
    const auto& a = leftPrepared.descriptors;
    const auto& b = rightPrepared.descriptors;
    MotionMatch result;
    result.leftIndex = leftIndex;
    result.rightIndex = rightIndex;
    result.leftSourceId = left.sourceId;
    result.rightSourceId = right.sourceId;
    result.dtwDistance = std::numeric_limits<double>::infinity();
    result.durationSeconds = std::min(duration(left), duration(right));
    result.leftStartSeconds = left.frames.empty() ? 0.0 : left.frames.front().timestampSeconds;
    result.leftEndSeconds = left.frames.empty() ? 0.0 : left.frames.back().timestampSeconds;
    result.rightStartSeconds = right.frames.empty() ? 0.0 : right.frames.front().timestampSeconds;
    result.rightEndSeconds = right.frames.empty() ? 0.0 : right.frames.back().timestampSeconds;
    if (left.sourceId == right.sourceId
        && std::abs(result.leftStartSeconds - result.rightStartSeconds)
            < std::max({params.sameSourceGapFloorSec, params.sameFileGapSec,
                        params.minRepeatGapSec})) {
        result.similarity = 0.0;
        return result;
    }
    if (a.empty() || b.empty()
        || leftPrepared.motionDelta < params.motionDeltaThreshold
        || rightPrepared.motionDelta < params.motionDeltaThreshold
        || !hasTemporalSupport(left, params)
        || !hasTemporalSupport(right, params)
        || !hasTemporalRun(a, b, left, right, params)) {
        result.similarity = 0.0;
        return result;
    }
    const std::size_t rows = a.size(), cols = b.size();
    const double noise = params.noiseFactor * std::max(noiseFloor(a), noiseFloor(b));
    const double inf = std::numeric_limits<double>::infinity();
    std::vector<double> previous(cols + 1, inf), current(cols + 1, inf);
    std::vector<std::size_t> previousSteps(cols + 1, 0), currentSteps(cols + 1, 0);
    previous[0] = 0.0;
    const std::size_t ratioBand = std::max<std::size_t>(2, static_cast<std::size_t>(std::ceil(
        static_cast<double>(std::max(rows, cols)) * params.sakoeChibaRatio)));
    const std::size_t band = std::max({params.dtwBand, ratioBand,
                                       rows > cols ? rows - cols : cols - rows});
    for (std::size_t i = 1; i <= rows; ++i) {
        std::fill(current.begin(), current.end(), inf);
        std::fill(currentSteps.begin(), currentSteps.end(), 0);
        const std::size_t begin = i > band ? i - band : 1;
        const std::size_t end = std::min(cols, i + band);
        for (std::size_t j = begin; j <= end; ++j) {
            const double cost = std::max(0.0, frameDistance(a[i - 1], b[j - 1]) - noise);
            double best = previous[j - 1];
            std::size_t bestSteps = previousSteps[j - 1];
            if (previous[j] < best) {
                best = previous[j];
                bestSteps = previousSteps[j];
            }
            if (current[j - 1] < best) {
                best = current[j - 1];
                bestSteps = currentSteps[j - 1];
            }
            if (std::isfinite(best)) {
                current[j] = cost + best;
                currentSteps[j] = bestSteps + 1;
            }
        }
        previous.swap(current);
        previousSteps.swap(currentSteps);
    }
    if (!std::isfinite(previous[cols]) || previousSteps[cols] == 0) return result;
    result.dtwDistance = previous[cols] / static_cast<double>(previousSteps[cols]);
    const double leftDuration = duration(left);
    const double rightDuration = duration(right);
    const double durationDenominator = std::max({leftDuration, rightDuration, 1e-9});
    const double timePenalty = params.timeWeight
        * std::abs(leftDuration - rightDuration) / durationDenominator;
    const double dtwScore = std::clamp(std::exp(-result.dtwDistance), 0.0, 1.0);
    const double temporalScore = temporalCosineScore(a, b);
    const double anatomyScore = shapeSimilarity(leftPrepared.poses, rightPrepared.poses);
    // DTW captures trajectory distance, cosine captures frame-wise direction,
    // and anatomyScore prevents different skeleton topology/proportions from
    // receiving a near-perfect score.  Keep a headroom below 100% so exact
    // duplicate frames cannot be presented as mathematically perfect proof.
    double calibrated = 0.50 * dtwScore + 0.30 * temporalScore
        + 0.20 * anatomyScore - timePenalty;
    // Reserve the 95%+ band for agreement across all three signals.  A pair
    // with a good average but a weak temporal or anatomical component is a
    // candidate, never an "almost identical" movement.
    if (calibrated > 0.95
        && (dtwScore < 0.96 || temporalScore < 0.96 || anatomyScore < 0.96)) {
        calibrated = 0.949;
    }
    result.similarity = std::clamp(calibrated, 0.0, 0.994);
    return result;
}

} // namespace

MotionMatcher::MotionMatcher(MotionMatcherParams params) : params_(params) { setParams(params); }

void MotionMatcher::setParams(MotionMatcherParams params)
{
    if (!(params.similarityThreshold >= 0.0 && params.similarityThreshold <= 1.0)
        || !(params.candidateThreshold >= 0.0 && params.candidateThreshold <= 1.0)
        || params.maxUniqueResults == 0 || params.dtwBand == 0 || params.noiseFactor < 0.0)
        throw std::invalid_argument("MotionMatcher: invalid parameters");
    if (!(params.sakoeChibaRatio >= 0.0 && params.sakoeChibaRatio <= 1.0)
        || !(params.minRepeatGapSec >= 0.0 && params.sameFileGapSec >= 0.0)
        || !(params.crossFileGapSec >= 0.0 && params.duplicateWindowSec >= 0.0)
        || !(params.timeWeight >= 0.0 && params.timeWeight <= 1.0)
        || !(params.motionDeltaThreshold >= 0.0 && params.motionDeltaThreshold <= 1.0)
        || !(params.temporalSimilarityThreshold >= 0.0
             && params.temporalSimilarityThreshold <= 1.0)
        || params.minTemporalFrames < 3
        || params.minTemporalDurationSec < 0.0
        || params.sameSourceGapFloorSec < 0.0
        || !(params.nmsOverlapThreshold >= 0.0 && params.nmsOverlapThreshold <= 1.0))
        throw std::invalid_argument("MotionMatcher: invalid temporal parameters");
    params_ = params;
}

MotionMatch MotionMatcher::compare(const MotionWindow& left, const MotionWindow& right,
                                   std::size_t leftIndex, std::size_t rightIndex) const
{
    const auto leftPoses = normalizePoses(left, params_.normalizeSize);
    const auto rightPoses = normalizePoses(right, params_.normalizeSize);
    return comparePrepared(left, right,
                           {leftPoses, describe(leftPoses, left), {}, motionDelta(left, leftPoses)},
                           {rightPoses, describe(rightPoses, right), {}, motionDelta(right, rightPoses)},
                           params_, leftIndex, rightIndex);
}

std::vector<MotionMatch> MotionMatcher::findAllPairs(const std::vector<MotionWindow>& windows) const
{
    std::vector<MotionMatch> matches;
    if (windows.size() < 2) return matches;

    std::vector<PreparedWindow> prepared(windows.size());
    std::size_t embeddingDimension = 0;
    for (std::size_t index = 0; index < windows.size(); ++index) {
        if (windows[index].frames.empty()) continue;
        const auto poses = normalizePoses(windows[index], params_.normalizeSize);
        prepared[index].poses = poses;
        prepared[index].descriptors = describe(poses, windows[index]);
        prepared[index].motionDelta = motionDelta(windows[index], poses);
        if (prepared[index].motionDelta < params_.motionDeltaThreshold
            || !hasTemporalSupport(windows[index], params_)) {
            prepared[index].descriptors.clear();
            continue;
        }
        for (const auto& descriptor : prepared[index].descriptors)
            embeddingDimension = std::max(embeddingDimension, descriptor.size());
    }
    if (embeddingDimension == 0) return matches;

    MotionIndex index;
    for (std::size_t windowIndex = 0; windowIndex < prepared.size(); ++windowIndex) {
        auto& item = prepared[windowIndex];
        if (item.descriptors.empty()) continue;
        item.embedding = embedding(item.descriptors, embeddingDimension);
        index.add(windowIndex, item.embedding);
    }
    index.build();

    // HNSW returns a bounded candidate set. The union of both query
    // directions keeps the all-pairs semantics while making DTW the expensive
    // second pass rather than the first operation on every pair.
    std::unordered_set<std::uint64_t> candidatePairs;
    const std::size_t candidateCount = std::min<std::size_t>(windows.size(),
        std::max<std::size_t>(8, std::min<std::size_t>(32, windows.size())));
    for (std::size_t i = 0; i < windows.size(); ++i) {
        if (prepared[i].descriptors.empty()) continue;
        for (const auto neighbour : index.query(prepared[i].embedding, candidateCount, candidateCount * 2)) {
            if (neighbour.id == i || neighbour.similarity + 1e-9 < params_.candidateThreshold) continue;
            const auto left = std::min(i, neighbour.id);
            const auto right = std::max(i, neighbour.id);
            candidatePairs.insert((static_cast<std::uint64_t>(left) << 32U)
                                  | static_cast<std::uint64_t>(right));
        }
    }

    for (const std::uint64_t key : candidatePairs) {
        const std::size_t i = static_cast<std::size_t>(key >> 32U);
        const std::size_t j = static_cast<std::size_t>(key & 0xffffffffULL);
        if (i >= windows.size() || j >= windows.size() || i >= j) continue;
        const bool sameSource = windows[i].sourceId == windows[j].sourceId;
        const double leftStart = windows[i].frames.front().timestampSeconds;
        const double rightStart = windows[j].frames.front().timestampSeconds;
        const double gap = std::abs(leftStart - rightStart);
        const double requiredGap = sameSource
            ? std::max({params_.sameSourceGapFloorSec, params_.sameFileGapSec,
                        params_.minRepeatGapSec})
            : params_.crossFileGapSec;
        if (gap < requiredGap) continue;
        if (coarseSimilarity(prepared[i].descriptors, prepared[j].descriptors)
            < params_.candidateThreshold) continue;
        MotionMatch candidate = comparePrepared(windows[i], windows[j], prepared[i], prepared[j],
                                                params_, i, j);
        if (candidate.similarity >= params_.similarityThreshold) matches.push_back(candidate);
    }
    std::sort(matches.begin(), matches.end(), [](const auto& a, const auto& b) {
        if (std::abs(a.similarity - b.similarity) > 1e-12) return a.similarity > b.similarity;
        if (a.leftIndex != b.leftIndex) return a.leftIndex < b.leftIndex;
        return a.rightIndex < b.rightIndex;
    });
    // Keep the strongest result for overlapping windows.  A window may still
    // participate in multiple independent pairs; only near-identical pairs
    // are removed, which is the semantics of duplicateWindowSec.
    std::vector<MotionMatch> unique;
    unique.reserve(matches.size());
    for (const MotionMatch& candidate : matches) {
        const bool duplicate = std::any_of(unique.begin(), unique.end(), [&](const MotionMatch& kept) {
            const bool sameOrientation = windows[candidate.leftIndex].sourceId == windows[kept.leftIndex].sourceId
                && windows[candidate.rightIndex].sourceId == windows[kept.rightIndex].sourceId;
            const bool swappedOrientation = windows[candidate.leftIndex].sourceId == windows[kept.rightIndex].sourceId
                && windows[candidate.rightIndex].sourceId == windows[kept.leftIndex].sourceId;
            if (!sameOrientation && !swappedOrientation) return false;
            const double leftDelta = std::abs(candidate.leftStartSeconds - kept.leftStartSeconds);
            const double rightDelta = std::abs(candidate.rightStartSeconds - kept.rightStartSeconds);
            const auto overlapRatio = [](double firstStart, double firstEnd,
                                         double secondStart, double secondEnd) {
                const double overlap = std::max(0.0, std::min(firstEnd, secondEnd)
                    - std::max(firstStart, secondStart));
                const double shorter = std::min(std::max(0.0, firstEnd - firstStart),
                                                std::max(0.0, secondEnd - secondStart));
                return shorter <= 1e-9 ? 0.0 : overlap / shorter;
            };
            const bool overlapping = overlapRatio(candidate.leftStartSeconds,
                                                  candidate.leftEndSeconds,
                                                  kept.leftStartSeconds,
                                                  kept.leftEndSeconds)
                >= params_.nmsOverlapThreshold
                && overlapRatio(candidate.rightStartSeconds, candidate.rightEndSeconds,
                                kept.rightStartSeconds, kept.rightEndSeconds)
                    >= params_.nmsOverlapThreshold;
            return overlapping || (leftDelta <= params_.duplicateWindowSec
                                   && rightDelta <= params_.duplicateWindowSec);
        });
        if (!duplicate) unique.push_back(candidate);
        if (unique.size() >= params_.maxUniqueResults) break;
    }
    matches = std::move(unique);
    return matches;
}

} // namespace pfcore
