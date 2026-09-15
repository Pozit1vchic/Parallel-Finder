#include "pfcore/MotionMatcher.hpp"
#include "pfcore/MotionIndex.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>
#include <unordered_set>

namespace pfcore {
namespace {

using Descriptor = std::vector<double>;

using NormalizedPose = std::vector<std::pair<double, double>>;

double appearanceCosine(const std::vector<float>& left,
                         const std::vector<float>& right)
{
    if (left.empty() || right.empty() || left.size() != right.size()) return 0.0;
    double dot = 0.0;
    double leftNorm = 0.0;
    double rightNorm = 0.0;
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (!std::isfinite(left[index]) || !std::isfinite(right[index])) return 0.0;
        dot += static_cast<double>(left[index]) * right[index];
        leftNorm += static_cast<double>(left[index]) * left[index];
        rightNorm += static_cast<double>(right[index]) * right[index];
    }
    if (leftNorm <= 1e-12 || rightNorm <= 1e-12) return 0.0;
    return std::clamp(dot / std::sqrt(leftNorm * rightNorm), -1.0, 1.0);
}

// Tracker IDs are only meaningful inside a continuous shot.  The detector
// runs before scene boundaries are known, so a person in the next shot can
// inherit the previous shot's numeric ID when the boxes happen to overlap.
// Treat a scene transition as an identity boundary unless body-ReID confirms
// that the two observations are still the same person.
bool differentTrackSegment(const MotionWindow& left, const MotionWindow& right)
{
    if (left.trackId != right.trackId) return true;
    return left.hasSceneIndex && right.hasSceneIndex
        && left.sceneIndex != right.sceneIndex;
}

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

struct MotionActivity {
    double meanDelta = 0.0;
    double activeTransitionRatio = 0.0;
    double trajectoryRange = 0.0;
};

struct FrameRoot {
    double x = 0.0;
    double y = 0.0;
    double scale = 1.0;
};

FrameRoot frameRoot(const PoseFrame& frame)
{
    if (frame.keypoints.empty()) return {};
    double cx = 0.0;
    double cy = 0.0;
    double weight = 0.0;
    for (const auto& point : frame.keypoints) {
        const double confidence = std::max(0.0, point.confidence);
        cx += point.x * confidence;
        cy += point.y * confidence;
        weight += confidence;
    }
    if (weight <= 1e-9) weight = static_cast<double>(frame.keypoints.size());
    cx /= weight;
    cy /= weight;
    double scale = 0.0;
    for (const auto& point : frame.keypoints)
        scale = std::max(scale, std::hypot(point.x - cx, point.y - cy));
    return {cx, cy, std::max(scale, 1e-6)};
}

MotionActivity motionActivity(const MotionWindow& window,
                              const std::vector<NormalizedPose>& poses)
{
    if (poses.size() < 2) return {};
    double total = 0.0;
    std::size_t transitions = 0;
    std::size_t activeTransitions = 0;
    double rangeSum = 0.0;
    std::size_t rangePoints = 0;
    if (!poses.empty() && !poses.front().empty()) {
        const std::size_t pointCount = poses.front().size();
        for (std::size_t point = 0; point < pointCount; ++point) {
            double minX = poses.front()[point].first;
            double maxX = minX;
            double minY = poses.front()[point].second;
            double maxY = minY;
            for (const auto& pose : poses) {
                if (pose.size() <= point) continue;
                minX = std::min(minX, pose[point].first);
                maxX = std::max(maxX, pose[point].first);
                minY = std::min(minY, pose[point].second);
                maxY = std::max(maxY, pose[point].second);
            }
            rangeSum += std::hypot(maxX - minX, maxY - minY);
            ++rangePoints;
        }
    }
    // A pose normalizer intentionally removes translation and scale. Keep a
    // separate relative root trajectory for the motion gate so a person
    // walking through the frame is not mistaken for a static pose.
    const FrameRoot baseRoot = frameRoot(window.frames.front());
    double rootMinX = 0.0, rootMaxX = 0.0, rootMinY = 0.0, rootMaxY = 0.0;
    double rootMinScale = 0.0, rootMaxScale = 0.0;
    if (!window.frames.empty()) {
        const double baseScale = std::max(baseRoot.scale, 1e-6);
        for (const auto& frame : window.frames) {
            const FrameRoot root = frameRoot(frame);
            const double relativeX = (root.x - baseRoot.x) / baseScale;
            const double relativeY = (root.y - baseRoot.y) / baseScale;
            const double relativeScale = std::log(std::max(root.scale, 1e-6) / baseScale);
            rootMinX = std::min(rootMinX, relativeX);
            rootMaxX = std::max(rootMaxX, relativeX);
            rootMinY = std::min(rootMinY, relativeY);
            rootMaxY = std::max(rootMaxY, relativeY);
            rootMinScale = std::min(rootMinScale, relativeScale);
            rootMaxScale = std::max(rootMaxScale, relativeScale);
        }
        const double rootRange = std::hypot(rootMaxX - rootMinX, rootMaxY - rootMinY)
            + 0.5 * std::abs(rootMaxScale - rootMinScale);
        rangeSum += rootRange;
        ++rangePoints;
    }
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
        const FrameRoot previousRoot = frameRoot(window.frames[frame - 1]);
        const FrameRoot currentRoot = frameRoot(window.frames[frame]);
        const double rootScale = std::max({baseRoot.scale, previousRoot.scale,
                                           currentRoot.scale, 1e-6});
        const double rootDelta = std::hypot(currentRoot.x - previousRoot.x,
                                            currentRoot.y - previousRoot.y) / rootScale
            + 0.5 * std::abs(std::log(std::max(currentRoot.scale, 1e-6)
                                      / std::max(previousRoot.scale, 1e-6)));
        const double mean = delta / static_cast<double>(valid) + rootDelta;
        total += mean;
        // Detector jitter is normally a few thousandths of a normalized
        // body unit. Require materially larger transitions and a sustained
        // ratio of them before calling a window a movement.
        if (mean >= 0.008) ++activeTransitions;
        ++transitions;
    }
    if (transitions == 0) return {};
    return {total / static_cast<double>(transitions),
            static_cast<double>(activeTransitions) / static_cast<double>(transitions),
            rangePoints == 0 ? 0.0 : rangeSum / static_cast<double>(rangePoints)};
}

std::vector<Descriptor> describe(const std::vector<NormalizedPose>& normalized,
                                 const MotionWindow& window)
{
    std::vector<Descriptor> result;
    result.reserve(normalized.size());
    const FrameRoot baseRoot = window.frames.empty() ? FrameRoot{}
        : frameRoot(window.frames.front());
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
        // Add a relative root/scale channel after the normalized joints. It
        // carries translation and zoom motion, but never absolute image
        // coordinates, so the same movement remains comparable in another
        // crop or camera framing.
        const FrameRoot currentRoot = frameRoot(window.frames[frameIndex]);
        const double baseScale = std::max(baseRoot.scale, 1e-6);
        const double rootX = (currentRoot.x - baseRoot.x) / baseScale;
        const double rootY = std::log(std::max(currentRoot.scale, 1e-6) / baseScale);
        double rootVelocityX = 0.0;
        double rootVelocityY = 0.0;
        if (frameIndex > 0) {
            const FrameRoot previousRoot = frameRoot(window.frames[frameIndex - 1]);
            const double previousScale = std::max(previousRoot.scale, 1e-6);
            const double dt = std::max(1e-3, window.frames[frameIndex].timestampSeconds
                - window.frames[frameIndex - 1].timestampSeconds);
            rootVelocityX = (rootX - (previousRoot.x - baseRoot.x) / baseScale) / dt;
            rootVelocityY = (rootY - std::log(previousScale / baseScale)) / dt;
        }
        descriptor.push_back(std::clamp(rootX, -8.0, 8.0));
        descriptor.push_back(std::clamp(rootY, -4.0, 4.0));
        descriptor.push_back(std::clamp(rootVelocityX, -8.0, 8.0));
        descriptor.push_back(std::clamp(rootVelocityY, -8.0, 8.0));
        result.push_back(std::move(descriptor));
    }
    return result;
}

double frameDistance(const Descriptor& left, const Descriptor& right)
{
    if (left.empty() || right.empty() || left.size() != right.size()
        || left.size() % 4U != 0U) return 1.0;

    // Descriptors are interleaved as x/y position and x/y velocity for each
    // joint.  A flat L1 average over all four values made unrelated poses
    // look deceptively close: most normalized coordinates are small and
    // exp(-distance) turned that small error into an 85–95% score.  Keep the
    // spatial and temporal errors separate and normalize velocity to its
    // bounded [-4, 4] range.
    double positionError = 0.0;
    double velocityError = 0.0;
    const std::size_t joints = left.size() / 4U;
    for (std::size_t joint = 0; joint < joints; ++joint) {
        const std::size_t offset = joint * 4U;
        positionError += std::hypot(left[offset] - right[offset],
                                    left[offset + 1U] - right[offset + 1U]);
        velocityError += std::hypot(left[offset + 2U] - right[offset + 2U],
                                    left[offset + 3U] - right[offset + 3U]) / 8.0;
    }
    const double spatial = positionError / static_cast<double>(joints);
    const double temporal = velocityError / static_cast<double>(joints);
    return std::clamp(0.78 * spatial + 0.22 * temporal, 0.0, 1.0);
}

double frameSimilarity(const Descriptor& left, const Descriptor& right)
{
    if (left.empty() || right.empty() || left.size() != right.size()) return 0.0;
    const double distance = frameDistance(left, right);
    if (!std::isfinite(distance)) return 0.0;
    // The exponential is intentionally steeper than the old exp(-distance).
    // A frame must agree in both joint layout and instantaneous velocity to
    // contribute to a temporal run.
    return std::clamp(std::exp(-2.0 * distance), 0.0, 1.0);
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
    // Four endpoint samples made a long window look like a single-frame
    // comparison. Use a denser temporal sketch before the expensive DTW pass.
    const std::size_t samples = std::min<std::size_t>(16, std::min(left.size(), right.size()));
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
    return used == 0 ? 0.0 : std::clamp(std::exp(-3.0 * distance
                                                        / static_cast<double>(used)), 0.0, 1.0);
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
        const double similarity = frameSimilarity(left[li], right[ri]);
        if (similarity <= 0.0) continue;
        score += similarity;
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
        || (span + 1e-9 >= std::max(params.minTemporalDurationSec, params.minMotionSpanSec)
            && window.frames.size() >= 6);
}

bool hasDistinctTemporalSamples(const MotionWindow& window,
                                const std::vector<Descriptor>& descriptors,
                                const MotionMatcherParams& params)
{
    // A candidate is never allowed to collapse to one reused frame. Count the
    // actual timestamped samples that reached the descriptor stage; this also
    // works for static-frame mode where pose motion is intentionally optional.
    if (descriptors.size() < params.minTemporalFrames
        || window.frames.size() < params.minTemporalFrames) return false;
    std::size_t usable = 0;
    double previousTimestamp = -std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < descriptors.size()
         && index < window.frames.size(); ++index) {
        if (descriptors[index].empty()) continue;
        const double timestamp = window.frames[index].timestampSeconds;
        if (!std::isfinite(timestamp) || timestamp <= previousTimestamp) continue;
        previousTimestamp = timestamp;
        ++usable;
    }
    return usable >= params.minTemporalFrames;
}

bool hasTemporalDiversity(const std::vector<Descriptor>& descriptors)
{
    // Counting timestamps alone is not enough: a detector can copy one pose
    // into dozens of frames. Require several genuinely new descriptor states
    // across the window. The threshold is below a meaningful body movement
    // but above ordinary one-frame keypoint jitter.
    if (descriptors.size() < 6) return false;
    constexpr double noveltyThreshold = 0.015;
    const Descriptor* anchor = nullptr;
    std::size_t distinctStates = 0;
    for (const auto& descriptor : descriptors) {
        if (descriptor.empty()) continue;
        if (!anchor || frameDistance(*anchor, descriptor) >= noveltyThreshold) {
            anchor = &descriptor;
            ++distinctStates;
        }
    }
    return distinctStates >= 4;
}

bool hasTemporalRun(const std::vector<Descriptor>& left,
                   const std::vector<Descriptor>& right,
                   const MotionWindow& leftWindow,
                   const MotionWindow& rightWindow,
                   const MotionMatcherParams& params)
{
    if (left.empty() || right.empty()) return false;
    const std::size_t leftSamples = left.size();
    const std::size_t rightSamples = right.size();
    if (leftSamples < 3 || rightSamples < 3) return false;
    const std::size_t band = std::max<std::size_t>(2,
        static_cast<std::size_t>(std::ceil(
            static_cast<double>(std::max(leftSamples, rightSamples))
            * std::max(0.10, params.sakoeChibaRatio * 2.0))));
    std::size_t run = 0;
    std::size_t bestRun = 0;
    std::size_t runStartLeft = 0;
    std::size_t runStartRight = 0;
    std::size_t bestStartLeft = 0;
    std::size_t bestEndLeft = 0;
    std::size_t bestStartRight = 0;
    std::size_t bestEndRight = 0;
    std::size_t previousRight = 0;
    bool havePrevious = false;
    double maximumSimilarity = 0.0;
    for (std::size_t leftIndex = 0; leftIndex < leftSamples; ++leftIndex) {
        const std::size_t expectedRight = (leftIndex * (rightSamples - 1))
            / std::max<std::size_t>(1, leftSamples - 1);
        const std::size_t begin = expectedRight > band ? expectedRight - band : 0;
        const std::size_t end = std::min(rightSamples - 1, expectedRight + band);
        double bestSimilarity = 0.0;
        std::size_t rightIndex = begin;
        for (std::size_t candidate = begin; candidate <= end; ++candidate) {
            // A source observation may be used at most once in the temporal
            // alignment. Allowing `candidate == previousRight` turns one
            // frame into a cheap match for an entire sequence—the exact
            // single-frame failure mode this matcher is meant to prevent.
            if (havePrevious && candidate <= previousRight) continue;
            const double similarity = frameSimilarity(left[leftIndex], right[candidate]);
            maximumSimilarity = std::max(maximumSimilarity, similarity);
            if (similarity > bestSimilarity) {
                bestSimilarity = similarity;
                rightIndex = candidate;
            }
        }
        if (bestSimilarity >= params.temporalSimilarityThreshold) {
            if (run == 0) {
                runStartLeft = leftIndex;
                runStartRight = rightIndex;
            }
            ++run;
            if (run > bestRun) {
                bestRun = run;
                bestStartLeft = runStartLeft;
                bestEndLeft = leftIndex;
                bestStartRight = runStartRight;
                bestEndRight = rightIndex;
            }
            previousRight = rightIndex;
            havePrevious = true;
        } else {
            run = 0;
            havePrevious = false;
        }
    }
    const double runDuration = std::min(
        leftWindow.frames[std::min(bestEndLeft, leftWindow.frames.size() - 1)].timestampSeconds
            - leftWindow.frames[std::min(bestStartLeft, leftWindow.frames.size() - 1)].timestampSeconds,
        rightWindow.frames[std::min(bestEndRight, rightWindow.frames.size() - 1)].timestampSeconds
            - rightWindow.frames[std::min(bestStartRight, rightWindow.frames.size() - 1)].timestampSeconds);
    const bool enoughFrames = bestRun >= params.minTemporalFrames
        || (bestRun >= std::max<std::size_t>(6, params.minTemporalFrames / 3)
            && runDuration + 1e-9 >= params.minTemporalDurationSec);
    if (std::getenv("PF_DEBUG_MATCHER") != nullptr) {
        std::fprintf(stderr, "PF_DEBUG_MATCHER temporal max=%.3f bestRun=%zu duration=%.3f threshold=%.3f\n",
                     maximumSimilarity, bestRun, runDuration, params.temporalSimilarityThreshold);
    }
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
    double activeTransitionRatio = 0.0;
    double trajectoryRange = 0.0;
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
    result.leftSceneStartSeconds = left.sceneStartSeconds >= 0.0
        ? left.sceneStartSeconds : result.leftStartSeconds;
    result.leftSceneEndSeconds = left.sceneEndSeconds > result.leftSceneStartSeconds
        ? left.sceneEndSeconds : result.leftEndSeconds;
    result.rightSceneStartSeconds = right.sceneStartSeconds >= 0.0
        ? right.sceneStartSeconds : result.rightStartSeconds;
    result.rightSceneEndSeconds = right.sceneEndSeconds > result.rightSceneStartSeconds
        ? right.sceneEndSeconds : result.rightEndSeconds;
    if (left.sourceId == right.sourceId
        && left.hasSceneIndex && right.hasSceneIndex
        && left.sceneIndex == right.sceneIndex) {
        result.similarity = 0.0;
        return result;
    }
    if (left.sourceId == right.sourceId
        && params.requireSameTrackWithinSource
        && left.trackId != 0 && right.trackId != 0
        && differentTrackSegment(left, right)) {
        const bool sameAppearance = !left.appearanceEmbedding.empty()
            && !right.appearanceEmbedding.empty()
            && appearanceCosine(left.appearanceEmbedding, right.appearanceEmbedding)
                >= params.minAppearanceSimilarity;
        if (!sameAppearance) {
            result.similarity = 0.0;
            return result;
        }
    }
    const bool hasAppearance = !left.appearanceEmbedding.empty()
        && !right.appearanceEmbedding.empty();
    if (params.requireAppearance && !hasAppearance) {
        result.similarity = 0.0;
        return result;
    }
    if (hasAppearance) {
        result.appearanceSimilarity = appearanceCosine(left.appearanceEmbedding,
                                                       right.appearanceEmbedding);
        result.appearanceVerified = result.appearanceSimilarity >= params.minAppearanceSimilarity;
        if (params.requireAppearance && !result.appearanceVerified) {
            result.similarity = 0.0;
            return result;
        }
    }
    if (left.sourceId == right.sourceId
        && std::abs(result.leftStartSeconds - result.rightStartSeconds)
            < std::max({params.sameSourceGapFloorSec, params.sameFileGapSec,
                        params.minRepeatGapSec})) {
        result.similarity = 0.0;
        return result;
    }
    const bool staticPair = left.staticFrameSet || right.staticFrameSet;
    const bool leftSamples = hasDistinctTemporalSamples(left, a, params);
    const bool rightSamples = hasDistinctTemporalSamples(right, b, params);
    const bool leftSupport = hasTemporalSupport(left, params);
    const bool rightSupport = hasTemporalSupport(right, params);
    const bool leftDiversity = staticPair || hasTemporalDiversity(a);
    const bool rightDiversity = staticPair || hasTemporalDiversity(b);
    const bool temporalRun = hasTemporalRun(a, b, left, right, params);
    const bool motionGateFails = leftPrepared.motionDelta < params.motionDeltaThreshold
        || rightPrepared.motionDelta < params.motionDeltaThreshold
        || leftPrepared.activeTransitionRatio < params.minActiveTransitionRatio
        || rightPrepared.activeTransitionRatio < params.minActiveTransitionRatio
        || leftPrepared.trajectoryRange < params.minMotionRange
        || rightPrepared.trajectoryRange < params.minMotionRange
        || !temporalRun;
    if (left.staticFrameSet != right.staticFrameSet
        || (staticPair && !params.allowStaticFrames)
        || a.empty() || b.empty()
        || !leftSamples || !rightSamples
        || !leftSupport || !rightSupport
        || !leftDiversity || !rightDiversity
        || (!staticPair && motionGateFails)) {
        if (std::getenv("PF_DEBUG_MATCHER") != nullptr) {
            std::fprintf(stderr,
                         "PF_DEBUG_MATCHER reject a=%zu b=%zu samples=%d/%d support=%d/%d diversity=%d/%d motion=%.4f/%.4f range=%.4f/%.4f active=%.3f/%.3f run=%d\n",
                         leftIndex, rightIndex, leftSamples ? 1 : 0, rightSamples ? 1 : 0,
                         leftSupport ? 1 : 0, rightSupport ? 1 : 0,
                         leftDiversity ? 1 : 0, rightDiversity ? 1 : 0,
                         leftPrepared.motionDelta, rightPrepared.motionDelta,
                         leftPrepared.trajectoryRange, rightPrepared.trajectoryRange,
                         leftPrepared.activeTransitionRatio,
                         rightPrepared.activeTransitionRatio, temporalRun ? 1 : 0);
        }
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
    const double dtwScore = std::clamp(std::exp(-3.0 * result.dtwDistance), 0.0, 1.0);
    const double temporalScore = temporalCosineScore(a, b);
    const double anatomyScore = shapeSimilarity(leftPrepared.poses, rightPrepared.poses);
    // DTW captures trajectory distance, cosine captures frame-wise direction,
    // and anatomyScore prevents different skeleton topology/proportions from
    // receiving a near-perfect score.  Keep a headroom below 100% so exact
    // duplicate frames cannot be presented as mathematically perfect proof.
    const double motionScore = 0.50 * dtwScore + 0.30 * temporalScore
        + 0.20 * anatomyScore;
    double calibrated = motionScore;
    if (result.appearanceVerified) {
        calibrated = (1.0 - params.appearanceWeight) * motionScore
            + params.appearanceWeight * result.appearanceSimilarity;
    }
    calibrated -= timePenalty;
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
        || !(params.minActiveTransitionRatio >= 0.0 && params.minActiveTransitionRatio <= 1.0)
        || params.minMotionRange < 0.0
        || params.minMotionSpanSec < 0.0
        || !(params.temporalSimilarityThreshold >= 0.0
             && params.temporalSimilarityThreshold <= 1.0)
        || params.minTemporalFrames < 3
        || params.minTemporalDurationSec < 0.0
        || params.sameSourceGapFloorSec < 0.0
        || !(params.nmsOverlapThreshold >= 0.0 && params.nmsOverlapThreshold <= 1.0))
        throw std::invalid_argument("MotionMatcher: invalid temporal parameters");
    if (!(params.minAppearanceSimilarity >= -1.0
          && params.minAppearanceSimilarity <= 1.0)
        || !(params.appearanceWeight >= 0.0 && params.appearanceWeight <= 1.0))
        throw std::invalid_argument("MotionMatcher: invalid appearance parameters");
    params_ = params;
}

MotionMatch MotionMatcher::compare(const MotionWindow& left, const MotionWindow& right,
                                   std::size_t leftIndex, std::size_t rightIndex) const
{
    const auto leftPoses = normalizePoses(left, params_.normalizeSize);
    const auto rightPoses = normalizePoses(right, params_.normalizeSize);
    const MotionActivity leftActivity = motionActivity(left, leftPoses);
    const MotionActivity rightActivity = motionActivity(right, rightPoses);
    return comparePrepared(left, right,
                           {leftPoses, describe(leftPoses, left), {}, leftActivity.meanDelta,
                            leftActivity.activeTransitionRatio, leftActivity.trajectoryRange},
                           {rightPoses, describe(rightPoses, right), {}, rightActivity.meanDelta,
                            rightActivity.activeTransitionRatio, rightActivity.trajectoryRange},
                           params_, leftIndex, rightIndex);
}

std::vector<MotionMatch> MotionMatcher::findAllPairs(const std::vector<MotionWindow>& windows) const
{
    std::vector<MotionMatch> matches;
    if (windows.size() < 2) return matches;
    matches.reserve(std::min(params_.maxUniqueResults, windows.size()));

    std::vector<PreparedWindow> prepared(windows.size());
    std::size_t preparedCount = 0;
    std::size_t embeddingDimension = 0;
    for (std::size_t index = 0; index < windows.size(); ++index) {
        if (windows[index].frames.empty()) continue;
        const auto poses = normalizePoses(windows[index], params_.normalizeSize);
        prepared[index].poses = poses;
        prepared[index].descriptors = describe(poses, windows[index]);
        const MotionActivity activity = motionActivity(windows[index], poses);
        prepared[index].motionDelta = activity.meanDelta;
        prepared[index].activeTransitionRatio = activity.activeTransitionRatio;
        prepared[index].trajectoryRange = activity.trajectoryRange;
        const bool staticWindow = windows[index].staticFrameSet;
        if ((staticWindow && !params_.allowStaticFrames)
            || !hasDistinctTemporalSamples(windows[index], prepared[index].descriptors, params_)
            || (!staticWindow && (prepared[index].motionDelta < params_.motionDeltaThreshold
                                  || prepared[index].activeTransitionRatio < params_.minActiveTransitionRatio
                                  || prepared[index].trajectoryRange < params_.minMotionRange
                                  || !hasTemporalDiversity(prepared[index].descriptors)))
            || !hasTemporalSupport(windows[index], params_)) {
            prepared[index].descriptors.clear();
            continue;
        }
        ++preparedCount;
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
    candidatePairs.reserve(preparedCount * std::min<std::size_t>(candidateCount, 32U));
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

    std::size_t gapPassed = 0;
    std::size_t coarsePassed = 0;
    std::size_t compared = 0;
    std::size_t trackRejected = 0;
    std::size_t sceneRejected = 0;
    std::size_t staticRejected = 0;
    for (const std::uint64_t key : candidatePairs) {
        const std::size_t i = static_cast<std::size_t>(key >> 32U);
        const std::size_t j = static_cast<std::size_t>(key & 0xffffffffULL);
        if (i >= windows.size() || j >= windows.size() || i >= j) continue;
        const bool sameSource = windows[i].sourceId == windows[j].sourceId;
        if (windows[i].staticFrameSet != windows[j].staticFrameSet) { ++staticRejected; continue; }
        if (sameSource && params_.requireSameTrackWithinSource
            && windows[i].trackId != 0 && windows[j].trackId != 0
            && differentTrackSegment(windows[i], windows[j])) {
            const bool sameAppearance = !windows[i].appearanceEmbedding.empty()
                && !windows[j].appearanceEmbedding.empty()
                && appearanceCosine(windows[i].appearanceEmbedding,
                                    windows[j].appearanceEmbedding)
                    >= params_.minAppearanceSimilarity;
            if (!sameAppearance) {
                ++trackRejected;
                continue;
            }
        }
        if (sameSource && windows[i].hasSceneIndex && windows[j].hasSceneIndex
            && windows[i].sceneIndex == windows[j].sceneIndex) {
            ++sceneRejected;
            continue;
        }
        const double leftStart = windows[i].frames.front().timestampSeconds;
        const double rightStart = windows[j].frames.front().timestampSeconds;
        const double gap = std::abs(leftStart - rightStart);
        const double requiredGap = sameSource
            ? std::max({params_.sameSourceGapFloorSec, params_.sameFileGapSec,
                        params_.minRepeatGapSec})
            : params_.crossFileGapSec;
        if (gap < requiredGap) continue;
        ++gapPassed;
        if (coarseSimilarity(prepared[i].descriptors, prepared[j].descriptors)
            < params_.candidateThreshold) continue;
        ++coarsePassed;
        ++compared;
        MotionMatch candidate = comparePrepared(windows[i], windows[j], prepared[i], prepared[j],
                                                params_, i, j);
        if (std::getenv("PF_DEBUG_MATCHER") != nullptr) {
            std::fprintf(stderr, "PF_DEBUG_MATCHER compared a=%zu b=%zu similarity=%.3f dtw=%.3f appearance=%.3f\n",
                         i, j, candidate.similarity, candidate.dtwDistance,
                         candidate.appearanceSimilarity);
        }
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
            const bool sameTrackAndScene = windows[candidate.leftIndex].trackId != 0
                && windows[candidate.leftIndex].trackId == windows[kept.leftIndex].trackId
                && windows[candidate.rightIndex].trackId != 0
                && windows[candidate.rightIndex].trackId == windows[kept.rightIndex].trackId
                && windows[candidate.leftIndex].hasSceneIndex
                && windows[candidate.rightIndex].hasSceneIndex
                && windows[candidate.leftIndex].sceneIndex == windows[kept.leftIndex].sceneIndex
                && windows[candidate.rightIndex].sceneIndex == windows[kept.rightIndex].sceneIndex;
            return overlapping || (leftDelta <= params_.duplicateWindowSec
                                   && rightDelta <= params_.duplicateWindowSec)
                || (sameTrackAndScene && leftDelta <= 1.25 && rightDelta <= 1.25);
        });
        if (!duplicate) unique.push_back(candidate);
        if (unique.size() >= params_.maxUniqueResults) break;
    }
    matches = std::move(unique);
    if (std::getenv("PF_DEBUG_MATCHER") != nullptr) {
        std::fprintf(stderr,
                     "PF_DEBUG_MATCHER windows=%zu prepared=%zu candidates=%zu trackRejected=%zu sceneRejected=%zu staticRejected=%zu gapPassed=%zu coarsePassed=%zu compared=%zu accepted=%zu\n",
                     windows.size(), preparedCount, candidatePairs.size(), trackRejected,
                     sceneRejected, staticRejected, gapPassed, coarsePassed, compared,
                     matches.size());
    }
    return matches;
}

} // namespace pfcore
