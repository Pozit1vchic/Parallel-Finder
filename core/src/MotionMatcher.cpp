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

constexpr double kMinimumKeypointConfidence = 0.25;
constexpr std::size_t kMinimumComparableJoints = 6;

bool validPoint(const std::pair<double, double>& point)
{
    return std::isfinite(point.first) && std::isfinite(point.second);
}

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

double sceneContextSimilarity(const MotionWindow& left, const MotionWindow& right)
{
    if (left.sceneContext.empty() || right.sceneContext.empty()
        || left.sceneContext.size() != right.sceneContext.size()) return 0.5;
    double context = 0.0;
    for (std::size_t i = 0; i < left.sceneContext.size(); ++i)
        context += std::sqrt(std::max(0.0F, left.sceneContext[i])
                             * std::max(0.0F, right.sceneContext[i]));
    return std::clamp(context, 0.0, 1.0);
}

// Tracker IDs are local to one source and scene cuts can restart a tracker.
// A different ID is therefore not identity proof in either direction; it
// needs multi-crop body-ReID confirmation before the temporal matcher sees it.
bool differentTrackSegment(const MotionWindow& left, const MotionWindow& right)
{
    if (left.trackId != right.trackId) return true;
    return left.hasSceneIndex && right.hasSceneIndex
        && left.sceneIndex != right.sceneIndex;
}

struct IdentityEvidence { bool available = false; bool verified = false; bool face = false; double score = 0; };

IdentityEvidence identityEvidence(const MotionWindow& left, const MotionWindow& right,
                                  const MotionMatcherParams& params)
{
    const bool face = !left.faceEmbedding.empty() && !right.faceEmbedding.empty()
        && left.faceConfidence >= params.minAppearanceEvidence
        && right.faceConfidence >= params.minAppearanceEvidence;
    if (face) {
        const double score = appearanceCosine(left.faceEmbedding, right.faceEmbedding);
        // Reliable face disagreement vetoes body resemblance, even when
        // uniforms/clothing make body descriptors nearly identical.
        return {true, score >= params.minFaceSimilarity, true, score};
    }
    const bool body = !left.appearanceEmbedding.empty() && !right.appearanceEmbedding.empty()
        && left.appearanceConfidence >= params.minAppearanceEvidence
        && right.appearanceConfidence >= params.minAppearanceEvidence;
    const double score = body ? appearanceCosine(left.appearanceEmbedding, right.appearanceEmbedding) : 0;
    return {body, body && score >= params.minAppearanceSimilarity, false, score};
}

bool separateVerifiedShots(const MotionWindow& left, const MotionWindow& right,
                           const MotionMatcherParams& params)
{
    if (left.sourceId != right.sourceId || !left.hasSceneIndex || !right.hasSceneIndex
        || left.sceneIndex == right.sceneIndex) return false;
    const auto validBounds = [](const MotionWindow& window) {
        return !window.frames.empty() && std::isfinite(window.sceneStartSeconds)
            && std::isfinite(window.sceneEndSeconds) && window.sceneStartSeconds >= 0.0
            && window.sceneEndSeconds > window.sceneStartSeconds
            && window.frames.front().timestampSeconds >= window.sceneStartSeconds
            && window.frames.back().timestampSeconds <= window.sceneEndSeconds;
    };
    return validBounds(left) && validBounds(right)
        && (left.sceneEndSeconds <= right.sceneStartSeconds
            || right.sceneEndSeconds <= left.sceneStartSeconds)
        && identityEvidence(left, right, params).verified;
}

std::vector<NormalizedPose> normalizePoses(const MotionWindow& window,
                                           bool normalizeSize)
{
    std::vector<NormalizedPose> normalized;
    normalized.reserve(window.frames.size());
    // COCO pose models share shoulder indices 5/6. A centre computed from
    // whichever joints happen to be visible jumps when a wrist disappears.
    // Anchor these models to the torso and use one robust scale per window.
    std::vector<double> torsoScales;
    for (const auto& frame : window.frames) {
        if (frame.keypoints.size() != 17) continue;
        const auto& a = frame.keypoints[5];
        const auto& b = frame.keypoints[6];
        if (a.confidence >= kMinimumKeypointConfidence
            && b.confidence >= kMinimumKeypointConfidence) {
            const double scale = std::hypot(a.x - b.x, a.y - b.y);
            if (std::isfinite(scale) && scale > 1e-6) torsoScales.push_back(scale);
        }
    }
    double torsoScale = 0.0;
    if (torsoScales.size() * 2 >= window.frames.size() && !torsoScales.empty()) {
        const auto middle = torsoScales.begin() + torsoScales.size() / 2;
        std::nth_element(torsoScales.begin(), middle, torsoScales.end());
        torsoScale = *middle;
    }
    for (const PoseFrame& frame : window.frames) {
        if (frame.keypoints.empty()) {
            normalized.emplace_back();
            continue;
        }
        double cx = 0.0, cy = 0.0, weight = 0.0;
        for (const auto& point : frame.keypoints) {
            if (!(point.confidence >= kMinimumKeypointConfidence)
                || !std::isfinite(point.x) || !std::isfinite(point.y)) continue;
            const double w = std::max(0.0, point.confidence);
            cx += point.x * w;
            cy += point.y * w;
            weight += w;
        }
        // A crop with no reliable joints cannot establish pose similarity.
        // Preserve it as invalid instead of turning zero-confidence values
        // into a plausible artificial skeleton.
        if (weight <= 1e-9) {
            normalized.emplace_back();
            continue;
        }
        cx /= weight;
        cy /= weight;
        if (torsoScale > 0.0 && frame.keypoints.size() == 17) {
            const auto& a = frame.keypoints[5];
            const auto& b = frame.keypoints[6];
            if (!(a.confidence >= kMinimumKeypointConfidence)
                || !(b.confidence >= kMinimumKeypointConfidence)
                || !std::isfinite(a.x + a.y + b.x + b.y)) {
                normalized.emplace_back();
                continue;
            }
            cx = (a.x + b.x) * 0.5;
            cy = (a.y + b.y) * 0.5;
        }
        double scale = 1.0;
        if (normalizeSize) {
            scale = 0.0;
            for (const auto& point : frame.keypoints) {
                if (point.confidence < kMinimumKeypointConfidence) continue;
                scale = std::max(scale, std::hypot(point.x - cx, point.y - cy));
            }
            if (scale <= 1e-9) scale = 1.0;
            if (torsoScale > 0.0) scale = torsoScale;
        }
        NormalizedPose pose;
        pose.reserve(frame.keypoints.size());
        for (const auto& point : frame.keypoints) {
            if (!(point.confidence >= kMinimumKeypointConfidence)
                || !std::isfinite(point.x) || !std::isfinite(point.y)) {
                const double invalid = std::numeric_limits<double>::quiet_NaN();
                pose.emplace_back(invalid, invalid);
            } else {
                pose.emplace_back((point.x - cx) / scale, (point.y - cy) / scale);
            }
        }
        normalized.push_back(std::move(pose));
    }
    return normalized;
}

struct MotionActivity {
    double meanDelta = 0.0;
    double activeTransitionRatio = 0.0;
    double trajectoryRange = 0.0;
};

double activeJointMean(std::vector<double> values)
{
    if (values.empty()) return 0.0;
    std::sort(values.begin(), values.end(), std::greater<double>());
    // A head turn or one moving arm should not be diluted by stationary
    // hips/legs. Still require a group of joints rather than one outlier.
    const auto count = std::min(values.size(), std::max<std::size_t>(3,
        (values.size() + 3) / 4));
    double sum = 0;
    for (std::size_t i = 0; i < count; ++i) sum += values[i];
    return sum / count;
}

struct FrameRoot {
    double x = 0.0;
    double y = 0.0;
    double scale = 1.0;
};

FrameRoot frameRoot(const PoseFrame& frame)
{
    if (frame.keypoints.empty()) return {};
    if (frame.keypoints.size() == 17) {
        const auto& a = frame.keypoints[5];
        const auto& b = frame.keypoints[6];
        if (a.confidence >= kMinimumKeypointConfidence
            && b.confidence >= kMinimumKeypointConfidence
            && std::isfinite(a.x + a.y + b.x + b.y))
            return {(a.x + b.x) * 0.5, (a.y + b.y) * 0.5,
                    std::max(1e-6, std::hypot(a.x - b.x, a.y - b.y))};
    }
    double cx = 0.0;
    double cy = 0.0;
    double weight = 0.0;
    for (const auto& point : frame.keypoints) {
        if (point.confidence < kMinimumKeypointConfidence) continue;
        const double confidence = std::max(0.0, point.confidence);
        cx += point.x * confidence;
        cy += point.y * confidence;
        weight += confidence;
    }
    if (weight <= 1e-9) return {};
    cx /= weight;
    cy /= weight;
    double scale = 0.0;
    for (const auto& point : frame.keypoints) {
        if (point.confidence < kMinimumKeypointConfidence) continue;
        scale = std::max(scale, std::hypot(point.x - cx, point.y - cy));
    }
    return {cx, cy, std::max(scale, 1e-6)};
}

MotionActivity motionActivity(const MotionWindow& window,
                              const std::vector<NormalizedPose>& poses)
{
    if (poses.size() < 2) return {};
    double total = 0.0;
    std::size_t transitions = 0;
    std::size_t activeTransitions = 0;
    std::vector<double> ranges;
    if (!poses.empty() && !poses.front().empty()) {
        const std::size_t pointCount = poses.front().size();
        for (std::size_t point = 0; point < pointCount; ++point) {
            bool hasPoint = false;
            double minX = 0.0, maxX = 0.0, minY = 0.0, maxY = 0.0;
            for (const auto& pose : poses) {
                if (pose.size() <= point || !validPoint(pose[point])) continue;
                if (!hasPoint) {
                    minX = maxX = pose[point].first;
                    minY = maxY = pose[point].second;
                    hasPoint = true;
                    continue;
                }
                minX = std::min(minX, pose[point].first);
                maxX = std::max(maxX, pose[point].first);
                minY = std::min(minY, pose[point].second);
                maxY = std::max(maxY, pose[point].second);
            }
            if (!hasPoint) continue;
            ranges.push_back(std::hypot(maxX - minX, maxY - minY));
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
        ranges.push_back(rootRange);
    }
    for (std::size_t frame = 1; frame < poses.size(); ++frame) {
        const auto& previous = poses[frame - 1];
        const auto& current = poses[frame];
        if (previous.empty() || current.empty() || previous.size() != current.size()) continue;
        double delta = 0.0;
        std::size_t valid = 0;
        for (std::size_t point = 0; point < current.size(); ++point) {
            if (window.frames[frame - 1].keypoints[point].confidence < kMinimumKeypointConfidence
                || window.frames[frame].keypoints[point].confidence < kMinimumKeypointConfidence
                || !validPoint(previous[point]) || !validPoint(current[point])) {
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
            activeJointMean(std::move(ranges))};
}

MotionWindow mirroredWindow(MotionWindow window)
{
    for (auto& frame : window.frames) {
        for (auto& point : frame.keypoints) {
            if (std::isfinite(point.x)) point.x = -point.x;
        }
        // COCO-17 left/right semantic joints must be swapped as well as X.
        if (frame.keypoints.size() == 17) {
            for (const auto [left, right] : {std::pair<std::size_t, std::size_t>{1, 2},
                                             {3, 4}, {5, 6}, {7, 8}, {9, 10},
                                             {11, 12}, {13, 14}, {15, 16}}) {
                std::swap(frame.keypoints[left], frame.keypoints[right]);
            }
        }
    }
    return window;
}

std::vector<Descriptor> describe(const std::vector<NormalizedPose>& normalized,
                                 const MotionWindow& window)
{
    std::vector<Descriptor> result;
    result.reserve(normalized.size());
    for (std::size_t frameIndex = 0; frameIndex < normalized.size(); ++frameIndex) {
        const auto& pose = normalized[frameIndex];
        if (pose.empty()) { result.emplace_back(); continue; }
        if (window.staticFrameSet) {
            Descriptor descriptor;
            for (const auto& point : pose) {
                descriptor.insert(descriptor.end(), {point.first, point.second, 0.0, 0.0});
            }
            result.push_back(std::move(descriptor));
            continue;
        }
        // The retrieval descriptor must describe *what the person does*, not
        // where they happen to stand in a shot. Keeping normalized x/y here
        // made the nearest-neighbour stage a pose/composition search: a
        // stationary close-up of the same actor beat a matching gesture from
        // a different scene. Store locally fitted joint velocities instead;
        // DTW can now align an arm raise, turn or step at different moments
        // and speeds without treating a held pose as a parallel.
        Descriptor descriptor;
        // Keep the actor's root trajectory as one additional pseudo-joint.
        // Normalized keypoints intentionally remove translation; without this
        // channel a walking/approach movement whose body shape stays rigid is
        // indistinguishable from a static pose.  The root is normalized by
        // the initial body scale, so it remains comparable across resolutions.
        descriptor.reserve(pose.size() * 4 + 4);
        for (std::size_t pointIndex = 0; pointIndex < pose.size(); ++pointIndex) {
            double velocityX = std::numeric_limits<double>::quiet_NaN();
            double velocityY = velocityX;
            double accelerationX = velocityX;
            double accelerationY = velocityX;
            // Fit velocity over a short time neighbourhood. Differencing raw
            // detections twice magnified pixel jitter and missing joints into
            // clipped accelerations, making even repeated gestures disagree.
            // Missing observations stay missing; only observed joints enter
            // the fit and no gap longer than 250 ms is bridged.
            if (validPoint(pose[pointIndex])) {
                double sumT = 0, sumTT = 0, sumX = 0, sumY = 0, sumTX = 0, sumTY = 0;
                std::size_t count = 0;
                const auto begin = frameIndex > 2 ? frameIndex - 2 : 0;
                const auto end = std::min(normalized.size(), frameIndex + 3);
                for (std::size_t sample = begin; sample < end; ++sample) {
                    if (normalized[sample].size() != pose.size()
                        || !validPoint(normalized[sample][pointIndex])) continue;
                    const double t = window.frames[sample].timestampSeconds
                        - window.frames[frameIndex].timestampSeconds;
                    if (std::abs(t) > 0.25) continue;
                    const auto& p = normalized[sample][pointIndex];
                    sumT += t; sumTT += t * t;
                    sumX += p.first; sumY += p.second;
                    sumTX += t * p.first; sumTY += t * p.second;
                    ++count;
                }
                const double denominator = count * sumTT - sumT * sumT;
                if (count >= 2 && denominator > 1e-9) {
                    velocityX = (count * sumTX - sumT * sumX) / denominator;
                    velocityY = (count * sumTY - sumT * sumY) / denominator;
                } else {
                    velocityX = velocityY = std::numeric_limits<double>::quiet_NaN();
                }
                accelerationX = accelerationY = 0.0;
            }
            descriptor.push_back(std::isfinite(velocityX) ? std::clamp(velocityX, -4.0, 4.0)
                                                          : velocityX);
            descriptor.push_back(std::isfinite(velocityY) ? std::clamp(velocityY, -4.0, 4.0)
                                                          : velocityY);
            descriptor.push_back(std::isfinite(accelerationX) ? std::clamp(accelerationX, -8.0, 8.0)
                                                              : accelerationX);
            descriptor.push_back(std::isfinite(accelerationY) ? std::clamp(accelerationY, -8.0, 8.0)
                                                              : accelerationY);
        }
        const FrameRoot currentRoot = frameRoot(window.frames[frameIndex]);
        const FrameRoot previousRoot = frameIndex > 0
            ? frameRoot(window.frames[frameIndex - 1]) : currentRoot;
        const double rootScale = std::max({frameRoot(window.frames.front()).scale,
                                           currentRoot.scale, previousRoot.scale, 1e-6});
        const double rootDt = frameIndex > 0
            ? std::max(1e-3, window.frames[frameIndex].timestampSeconds
                - window.frames[frameIndex - 1].timestampSeconds) : 1.0;
        const double rootVelocityX = frameIndex > 0
            ? (currentRoot.x - previousRoot.x) / rootScale / rootDt : 0.0;
        const double rootVelocityY = frameIndex > 0
            ? (currentRoot.y - previousRoot.y) / rootScale / rootDt : 0.0;
        const bool validRoot = frameIndex > 0 && !normalized[frameIndex - 1].empty();
        descriptor.push_back(validRoot ? std::clamp(rootVelocityX, -4.0, 4.0) : 0.0);
        descriptor.push_back(validRoot ? std::clamp(rootVelocityY, -4.0, 4.0) : 0.0);
        descriptor.push_back(0.0);
        descriptor.push_back(0.0);
        result.push_back(std::move(descriptor));
    }
    return result;
}

double frameDistance(const Descriptor& left, const Descriptor& right)
{
    if (left.empty() || right.empty() || left.size() != right.size()
        || left.size() % 4U != 0U) return 1.0;

    // Descriptors are interleaved x/y velocity and x/y acceleration for each
    // joint. This makes the distance a comparison of trajectories rather
    // than of the static body layout in a particular camera angle.
    double velocityError = 0.0;
    double accelerationError = 0.0;
    std::size_t comparableJoints = 0;
    std::size_t comparableVelocities = 0;
    const std::size_t joints = left.size() / 4U;
    for (std::size_t joint = 0; joint < joints; ++joint) {
        const std::size_t offset = joint * 4U;
        if (!std::isfinite(left[offset]) || !std::isfinite(left[offset + 1U])
            || !std::isfinite(right[offset]) || !std::isfinite(right[offset + 1U])) {
            continue;
        }
        // Velocities are expressed in normalized body units per second.  A
        // divisor of 8 made ordinary 0.1–0.3 units/s movements almost
        // indistinguishable (frameSimilarity stayed near 1.0 for unrelated
        // scenes).  Keep a moderate robust scale instead: small detector
        // jitter is still cheap, while the direction/amplitude of a real
        // gesture contributes materially to the distance.
        velocityError += std::hypot(left[offset] - right[offset],
                                    left[offset + 1U] - right[offset + 1U]) / 2.0;
        ++comparableJoints;
        if (std::isfinite(left[offset + 2U]) && std::isfinite(left[offset + 3U])
            && std::isfinite(right[offset + 2U]) && std::isfinite(right[offset + 3U])) {
            accelerationError += std::hypot(left[offset + 2U] - right[offset + 2U],
                                            left[offset + 3U] - right[offset + 3U]) / 4.0;
            ++comparableVelocities;
        }
    }
    // A head/shoulder crop lacks the body information needed to call two
    // scenes a matching motion. Failing closed beats inventing a percentage.
    const std::size_t minimumComparable = std::min(kMinimumComparableJoints, joints);
    if (comparableJoints < minimumComparable) return 1.0;
    const double velocity = velocityError / static_cast<double>(comparableJoints);
    const double acceleration = comparableVelocities == 0 ? 0.0
        : accelerationError / static_cast<double>(comparableVelocities);
    return std::clamp(0.75 * velocity + 0.25 * acceleration, 0.0, 1.0);
}

double frameSimilarity(const Descriptor& left, const Descriptor& right)
{
    if (left.empty() || right.empty() || left.size() != right.size()) return 0.0;
    // Stationary observations cannot establish a motion run even when their
    // descriptors agree perfectly. Static mode has its own pose descriptor.
    auto velocityEnergy = [](const Descriptor& descriptor) {
        double maximum = 0.0;
        for (std::size_t offset = 0; offset + 1U < descriptor.size(); offset += 4U) {
            if (!std::isfinite(descriptor[offset]) || !std::isfinite(descriptor[offset + 1U]))
                continue;
            maximum = std::max(maximum,
                               std::hypot(descriptor[offset], descriptor[offset + 1U]));
        }
        return maximum;
    };
    if (velocityEnergy(left) < 1e-4 || velocityEnergy(right) < 1e-4) return 0.0;
    const double distance = frameDistance(left, right);
    if (!std::isfinite(distance)) return 0.0;
    // Agreement is measured on motion, not appearance or body proportions.
    return std::clamp(std::exp(-4.0 * distance), 0.0, 1.0);
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

        // Joint order is the topology contract, not visibility. Two close-ups
        // can agree on every observed joint while both omit the legs. Penalize
        // asymmetric visibility, never joints absent from both observations.
        const std::size_t points = std::min(a.size(), b.size());
        std::vector<std::size_t> valid;
        valid.reserve(points);
        for (std::size_t i = 0; i < points; ++i) {
            if (validPoint(a[i]) && validPoint(b[i])) valid.push_back(i);
        }
        const std::size_t minimumComparable = std::min(kMinimumComparableJoints, points);
        if (valid.size() < minimumComparable) continue;
        std::size_t observed = 0;
        for (std::size_t i = 0; i < std::max(a.size(), b.size()); ++i) {
            if ((i < a.size() && validPoint(a[i]))
                || (i < b.size() && validPoint(b[i]))) ++observed;
        }
        const double topology = static_cast<double>(valid.size())
            / static_cast<double>(std::max<std::size_t>(1, observed));
        double error = 0.0;
        std::size_t pairs = 0;
        for (std::size_t leftIndex = 0; leftIndex < valid.size(); ++leftIndex) {
            for (std::size_t rightIndex = leftIndex + 1; rightIndex < valid.size(); ++rightIndex) {
                const std::size_t i = valid[leftIndex];
                const std::size_t j = valid[rightIndex];
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

// Joint articulation complements image-plane distances for foreshortened
// views. It is not an action classifier: only observed, homologous chains
// are compared, after the identity gate, and only in static-pose mode.
std::vector<NormalizedPose> articulationPoses(const MotionWindow& window)
{
    std::vector<NormalizedPose> result;
    if (!window.staticFrameSet) return result;
    for (const auto& frame : window.frames) {
        NormalizedPose pose;
        for (const auto& point : frame.keypoints) {
            // Angles amplify errors at a poorly localized elbow. Such a
            // chain is unknown, not contradictory or perfectly straight.
            const double missing = std::numeric_limits<double>::quiet_NaN();
            const bool observed = std::isfinite(point.confidence) && point.confidence >= 0.5;
            pose.emplace_back(observed ? point.x : missing, observed ? point.y : missing);
        }
        result.push_back(std::move(pose));
    }
    return result;
}

double articulationSimilarity(const std::vector<NormalizedPose>& left,
                              const std::vector<NormalizedPose>& right,
                              const std::vector<NormalizedPose>& leftContext,
                              const std::vector<NormalizedPose>& rightContext)
{
    if (left.empty() || right.empty()) return 0.0;
    constexpr std::size_t chains[4][4] = {
        {5, 7, 9, 6}, {6, 8, 10, 5}, {11, 13, 15, 12}, {12, 14, 16, 11}
    };
    const auto features = [](const NormalizedPose& pose, const auto& chain,
                             double (&out)[3]) {
        if (pose.size() != 17 || !validPoint(pose[0])) return false;
        for (const auto joint : chain) if (!validPoint(pose[joint])) return false;
        const auto vector = [&](std::size_t a, std::size_t b) {
            return std::pair{pose[b].first - pose[a].first,
                             pose[b].second - pose[a].second};
        };
        const auto dot = [](auto a, auto b) { return a.first*b.first + a.second*b.second; };
        const auto cross = [](auto a, auto b) { return a.first*b.second - a.second*b.first; };
        const auto across = vector(chain[0], chain[3]);
        const auto upper = vector(chain[0], chain[1]);
        const auto lower = vector(chain[1], chain[2]);
        const auto head = vector(chain[0], 0);
        const auto reach = vector(0, chain[2]);
        const double acrossLength = std::hypot(across.first, across.second);
        const double upperLength = std::hypot(upper.first, upper.second);
        const double lowerLength = std::hypot(lower.first, lower.second);
        const double reachLength = std::hypot(reach.first, reach.second);
        if (std::min({acrossLength, upperLength, lowerLength}) < 1e-6)
            return false;
        // The head supplies the up/down half-plane; reflection must not turn
        // a lowered arm into a raised arm with the same unsigned angle.
        const double up = cross(across, head);
        if (std::abs(up) < 1e-6 * acrossLength) return false;
        const double down = up > 0.0 ? -1.0 : 1.0;
        out[0] = std::atan2(down * cross(across, upper), dot(across, upper));
        out[1] = std::atan2(down * cross(upper, lower), dot(upper, lower));
        // A wrist touching the face is valid contrary evidence, not a
        // missing chain. Clamp the dimensionless ratio only for log(0).
        out[2] = std::log(std::max(reachLength / (upperLength + lowerLength), 1e-6));
        return std::isfinite(out[0]) && std::isfinite(out[1]) && std::isfinite(out[2]);
    };
    const std::size_t samples = std::min<std::size_t>(8, std::min(left.size(), right.size()));
    std::vector<double> agreements;
    for (std::size_t sample = 0; sample < samples; ++sample) {
        const auto& a = left[sample * (left.size()-1) / std::max<std::size_t>(1, samples-1)];
        const auto& b = right[sample * (right.size()-1) / std::max<std::size_t>(1, samples-1)];
        const auto& contextA = leftContext[sample * (leftContext.size()-1) / std::max<std::size_t>(1, samples-1)];
        const auto& contextB = rightContext[sample * (rightContext.size()-1) / std::max<std::size_t>(1, samples-1)];
        double worst = 0.0;
        std::size_t observed = 0;
        for (const auto& chain : chains) {
            double x[3], y[3];
            if (!features(a, chain, x) || !features(b, chain, y)) continue;
            const auto angularDistance = [](double angle) {
                return std::atan2(std::sin(angle), std::cos(angle));
            };
            const double shoulder = angularDistance(x[0]-y[0]);
            const double elbow = angularDistance(x[1]-y[1]);
            const double reach = x[2]-y[2];
            // Missing bilateral support may withhold a positive vote, but
            // must never erase disagreement in an already observed limb.
            worst = std::max(worst,
                0.5*(shoulder*shoulder + elbow*elbow) + 0.15*reach*reach);
            double relationError = 0.0;
            if (chain[0] == 5 || chain[0] == 6) {
                // Similar elbow flexion can mean aiming OR folded arms.
                // Require bilateral evidence and compare each elbow's
                // distance to the opposite wrist, in shoulder-width units.
                const std::size_t oppositeWrist = chain[0] == 5 ? 10 : 9;
                const auto relation = [&](const NormalizedPose& pose) {
                    if (pose.size() != 17 || !validPoint(pose[oppositeWrist])
                        || !validPoint(pose[chain[1]]) || !validPoint(pose[5])
                        || !validPoint(pose[6])) return std::numeric_limits<double>::quiet_NaN();
                    const auto distance = [&](std::size_t i, std::size_t j) {
                        return std::hypot(pose[i].first-pose[j].first, pose[i].second-pose[j].second);
                    };
                    const double scale = distance(5, 6);
                    if (scale < 1e-6) return std::numeric_limits<double>::quiet_NaN();
                    return std::log(std::max(distance(chain[1], oppositeWrist) / scale, 1e-6));
                };
                const double difference = relation(contextA) - relation(contextB);
                if (!std::isfinite(difference)) continue;
                relationError = 0.5 * difference * difference;
            }
            // Do not average away a contradictory second arm or leg.
            worst = std::max(worst, relationError);
            ++observed;
        }
        if (observed) agreements.push_back(std::exp(-2.0 * worst));
    }
    // A lucky single-frame detection is not sustained pose evidence.
    if (agreements.size() < MotionMatcherParams::minimumStaticSamples
        || agreements.size() * 4 < samples * 3) return 0.0;
    // Use the lower quartile, not a best frame or an average dominated by
    // one confidently mislocalized wrist. At least 75% must support it.
    std::sort(agreements.begin(), agreements.end());
    return agreements[(agreements.size() - 1) / 4];
}

struct TemporalAlignment {
    std::size_t run = 0;
    double averageSimilarity = 0.0;
    std::size_t startLeft = 0;
    std::size_t endLeft = 0;
    std::size_t startRight = 0;
    std::size_t endRight = 0;
    std::vector<std::pair<std::size_t, std::size_t>> path;
};

TemporalAlignment alignTemporal(const std::vector<Descriptor>& left,
                                 const std::vector<Descriptor>& right,
                                 double similarityThreshold)
{
    TemporalAlignment result;
    if (left.size() < 3 || right.size() < 3) return result;

    // A gesture can start at a different point inside two overlapping
    // windows, and one export may be sampled faster than the other.  Find the
    // longest monotonic run with a bounded one-to-many step instead of forcing
    // both windows onto the same diagonal.  Window sizes are small (normally
    // 12–30 samples), so O(n*m*4) is negligible next to DTW/inference.
    // Both axes may advance by two observations. This symmetric constraint
    // tolerates isolated detector dropouts and up to 2x tempo changes without
    // reusing one observation as evidence for a complete gesture.
    struct Cell { std::size_t run = 0; double score = 0; std::size_t parent = 0; };
    const std::size_t cols = right.size();
    std::vector<Cell> cells(left.size() * cols);
    std::size_t bestCell = 0;
    for (std::size_t i = 0; i < left.size(); ++i) {
        for (std::size_t j = 0; j < right.size(); ++j) {
            const double similarity = frameSimilarity(left[i], right[j]);
            if (similarity < similarityThreshold) continue;
            auto& cell = cells[i * cols + j];
            cell = {1, similarity, i * cols + j};
            for (std::size_t di = 1; di <= 2 && di <= i; ++di) {
                for (std::size_t dj = 1; dj <= 2 && dj <= j; ++dj) {
                    const auto previousIndex = (i - di) * cols + j - dj;
                    const auto& previous = cells[previousIndex];
                    if (!previous.run) continue;
                    const double score = previous.score + similarity;
                    if (previous.run + 1 > cell.run
                        || (previous.run + 1 == cell.run && score > cell.score))
                        cell = {previous.run + 1, score, previousIndex};
                }
            }
            const auto& best = cells[bestCell];
            if (cell.run > best.run || (cell.run == best.run && cell.score > best.score))
                bestCell = i * cols + j;
        }
    }
    if (!cells[bestCell].run) return result;
    result.run = cells[bestCell].run;
    result.averageSimilarity = cells[bestCell].score / result.run;
    for (std::size_t index = bestCell;; index = cells[index].parent) {
        result.path.emplace_back(index / cols, index % cols);
        if (cells[index].parent == index) break;
    }
    std::reverse(result.path.begin(), result.path.end());
    result.startLeft = result.path.front().first;
    result.startRight = result.path.front().second;
    result.endLeft = result.path.back().first;
    result.endRight = result.path.back().second;
    return result;
}


double velocityDirectionScore(const std::vector<Descriptor>& left,
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
        const auto& a = left[li];
        const auto& b = right[ri];
        if (a.empty() || b.empty() || a.size() != b.size()) continue;
        double dot = 0.0;
        double normA = 0.0;
        double normB = 0.0;
        // In COCO descriptors the last channel is global root translation.
        // A shared camera pan must not turn opposite head/limb trajectories
        // into the same gesture. Prefer anatomical direction when both
        // observations contain measurable articulation; retain the root for
        // rigid locomotion where normalized body joints barely move.
        const bool coco = a.size() == 72;
        const std::size_t bodyEnd = coco ? a.size() - 4U : a.size();
        for (std::size_t point = 0; point + 3U < bodyEnd; point += 4U) {
            if (!std::isfinite(a[point]) || !std::isfinite(a[point + 1U])
                || !std::isfinite(b[point]) || !std::isfinite(b[point + 1U])) continue;
            dot += a[point] * b[point] + a[point + 1U] * b[point + 1U];
            normA += a[point] * a[point] + a[point + 1U] * a[point + 1U];
            normB += b[point] * b[point] + b[point + 1U] * b[point + 1U];
        }
        if (coco && (normA <= 1e-4 || normB <= 1e-4)) {
            const auto root = bodyEnd;
            if (std::isfinite(a[root]) && std::isfinite(a[root + 1U])
                && std::isfinite(b[root]) && std::isfinite(b[root + 1U])) {
                dot += a[root] * b[root] + a[root + 1U] * b[root + 1U];
                normA += a[root] * a[root] + a[root + 1U] * a[root + 1U];
                normB += b[root] * b[root] + b[root + 1U] * b[root + 1U];
            }
        }
        if (normA <= 1e-8 || normB <= 1e-8) continue;
        score += std::clamp(dot / std::sqrt(normA * normB), -1.0, 1.0);
        ++used;
    }
    // Orthogonal directions are not "50% the same movement". Shifting the
    // cosine by +0.5 previously inflated weak directional agreement enough
    // to pass quick search when low-amplitude DTW distances looked good.
    // Opposite/orthogonal evidence contributes zero; only positive alignment
    // contributes to the score. Identical directions remain one.
    return used == 0 ? 0.0 : std::clamp(score / static_cast<double>(used), 0.0, 1.0);
}

double duration(const MotionWindow& window);

double activeBodyAgreement(const std::vector<Descriptor>& left,
                           const std::vector<Descriptor>& right)
{
    // COCO's 17 anatomical joints plus the root-translation pseudo-joint.
    // Shared camera/root motion must not make a hand gesture match a step.
    if (left.empty() || right.empty() || left.front().size() != 72 || right.front().size() != 72)
        return 1.0;
    const auto energy = [](const std::vector<Descriptor>& samples, const std::vector<Descriptor>& other) {
        std::vector<double> result(17, 0.0);
        for (std::size_t frame = 0; frame < std::min(samples.size(), other.size()); ++frame) {
            const auto& sample = samples[frame];
            if (sample.size() != 72 || other[frame].size() != 72) continue;
            for (std::size_t joint = 0; joint < 17; ++joint) {
                const auto offset = joint * 4;
                if (!std::isfinite(sample[offset]) || !std::isfinite(sample[offset + 1])) continue;
                // An unobserved limb is unknown, not stationary.
                if (!std::isfinite(other[frame][offset]) || !std::isfinite(other[frame][offset + 1])) continue;
                result[joint] += std::max(0.0, std::hypot(sample[offset], sample[offset + 1]) - 0.01);
            }
        }
        return result;
    };
    const auto a = energy(left, right), b = energy(right, left);
    double dot = 0.0, normA = 0.0, normB = 0.0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        dot += a[i] * b[i]; normA += a[i] * a[i]; normB += b[i] * b[i];
    }
    if (normA <= 1e-8 && normB <= 1e-8) return 1.0; // rigid-body locomotion
    if (normA <= 1e-8 || normB <= 1e-8) return 0.0;
    return std::clamp(dot / std::sqrt(normA * normB), 0.0, 1.0);
}

bool hasTemporalSupport(const MotionWindow& window,
                        const MotionMatcherParams& params)
{
    if (window.frames.size() < 3) return false;
    const double span = duration(window);
    if (window.staticFrameSet)
        return window.frames.size() >= MotionMatcherParams::minimumStaticSamples
            && span + 1e-9 >= MotionMatcherParams::minimumStaticSpanSeconds;
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
    const auto minimumSamples = window.staticFrameSet
        ? MotionMatcherParams::minimumStaticSamples : std::size_t{6};
    if (descriptors.size() < minimumSamples || window.frames.size() < minimumSamples) return false;
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
    if (window.staticFrameSet)
        return usable >= minimumSamples
            && duration(window) + 1e-9 >= MotionMatcherParams::minimumStaticSpanSeconds;
    return usable >= params.minTemporalFrames
        || (usable >= 6 && duration(window) + 1e-9
            >= std::max(params.minTemporalDurationSec, params.minMotionSpanSec));
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
    const auto alignment = alignTemporal(left, right, params.temporalSimilarityThreshold);
    const double runDuration = alignment.run == 0 ? 0.0 : std::min(
        leftWindow.frames[std::min(alignment.endLeft, leftWindow.frames.size() - 1)].timestampSeconds
            - leftWindow.frames[std::min(alignment.startLeft, leftWindow.frames.size() - 1)].timestampSeconds,
        rightWindow.frames[std::min(alignment.endRight, rightWindow.frames.size() - 1)].timestampSeconds
            - rightWindow.frames[std::min(alignment.startRight, rightWindow.frames.size() - 1)].timestampSeconds);
    // Require a real run, but do not force the entire 2.5 s analysis window
    // to align. Edits often share only one short gesture; six consecutive
    // observations is the hard floor and the quarter-window term scales it
    // for denser sampling.
    const std::size_t requiredFrames = std::max<std::size_t>(
        std::size_t{6},
        static_cast<std::size_t>(std::ceil(0.25
            * static_cast<double>(std::min(left.size(), right.size())))));
    const bool enoughFrames = alignment.run >= requiredFrames
        && runDuration + 1e-9 >= params.minTemporalDurationSec;
    if (std::getenv("PF_DEBUG_MATCHER") != nullptr) {
        std::fprintf(stderr, "PF_DEBUG_MATCHER temporal max=%.3f bestRun=%zu duration=%.3f threshold=%.3f\n",
                     alignment.averageSimilarity, alignment.run, runDuration,
                     params.temporalSimilarityThreshold);
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
        // Missing joints are NaN in the exact matcher. HNSW distances and
        // heap ordering require finite values; preserve missing-data checks
        // in verification and use a neutral value only for coarse retrieval.
        for (std::size_t i = 0; i < copyCount; ++i)
            result[sample * dimension + i] = std::isfinite(descriptor[i]) ? descriptor[i] : 0.0;
    }
    return result;
}

struct PreparedWindow {
    std::vector<NormalizedPose> poses;
    std::vector<NormalizedPose> articulation;
    std::vector<NormalizedPose> mirroredArticulation;
    std::vector<Descriptor> descriptors;
    std::vector<double> embedding;
    std::vector<NormalizedPose> mirroredPoses;
    std::vector<Descriptor> mirroredDescriptors;
    std::vector<double> mirroredEmbedding;
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
        && std::max(result.leftStartSeconds, result.rightStartSeconds)
            < std::min(result.leftEndSeconds, result.rightEndSeconds)) return result;
    if (left.sourceId == right.sourceId
        && left.hasSceneIndex && right.hasSceneIndex
        && left.sceneIndex == right.sceneIndex) {
        result.similarity = 0.0;
        return result;
    }
    const auto identity = identityEvidence(left, right, params);
    const double sceneSimilarity = sceneContextSimilarity(left, right);
    if (left.sourceId == right.sourceId
        && std::abs(result.leftStartSeconds - result.rightStartSeconds) <= params.sameSceneContextGapSec
        && sceneSimilarity >= params.sameSceneContextThreshold) return result;
    result.appearanceSimilarity = identity.score;
    result.sceneSimilarity = sceneSimilarity;
    result.appearanceVerified = identity.verified;
    result.faceVerified = identity.verified && identity.face;
    // Close-up comparison has its own observable region. Do not normalize a
    // five-landmark head crop by its head radius and the other shot by torso
    // width. This fallback is pose-only, requires independent face identity,
    // and never promotes a held head pose to a repeated body movement.
    if (left.staticFrameSet && right.staticFrameSet && result.faceVerified) {
        auto headOnly = [](const MotionWindow& window) {
            if (window.frames.empty()) return false;
            std::size_t closeups = 0;
            for (const auto& frame : window.frames) {
                if (frame.keypoints.size() != 17) return false;
                std::size_t visible = 0;
                for (const auto& p : frame.keypoints)
                    if (p.confidence >= kMinimumKeypointConfidence
                        && std::isfinite(p.x) && std::isfinite(p.y)) ++visible;
                if (visible < kMinimumComparableJoints) ++closeups;
            }
            return closeups * 2 >= window.frames.size();
        };
        if (headOnly(left) || headOnly(right)) {
            auto head = [](MotionWindow window) {
                for (auto& frame : window.frames) {
                    if (frame.keypoints.size() != 17) { frame.keypoints.clear(); continue; }
                    frame.keypoints.resize(5);
                }
                return window;
            };
            const auto headLeft = head(left), headRight = head(right);
            auto headMatch = MotionMatcher(params).compare(headLeft, headRight, leftIndex, rightIndex);
            headMatch.headOnlyComparison = true;
            return headMatch;
        }
    }
    const bool crossTrack = left.sourceId == right.sourceId
        && params.requireSameTrackWithinSource && left.trackId != 0 && right.trackId != 0
        && differentTrackSegment(left, right);
    // A required identity check must fail closed: missing/weak evidence is
    // not proof that two motions belong to the same person. Costume handling
    // can supply body evidence, but must not silently bypass this gate.
    if ((crossTrack || params.requireAppearance) && !identity.verified)
        return result;
    if (left.sourceId == right.sourceId
        && !separateVerifiedShots(left, right, params)
        && std::abs(result.leftStartSeconds - result.rightStartSeconds)
            < std::max({params.sameSourceGapFloorSec, params.sameFileGapSec,
                        params.minRepeatGapSec})) {
        result.similarity = 0.0;
        return result;
    }
    const bool staticPair = left.staticFrameSet || right.staticFrameSet;
    if (staticPair && std::getenv("PF_DEBUG_MATCHER") != nullptr) {
        auto mask = [](const std::vector<NormalizedPose>& poses) {
            std::uint32_t bits = 0;
            if (!poses.empty()) for (std::size_t i = 0; i < poses.front().size() && i < 32; ++i)
                if (validPoint(poses.front()[i])) bits |= (1U << i);
            return bits;
        };
        std::fprintf(stderr, "PF_DEBUG_MATCHER pose-support a=%zu b=%zu mask=%x/%x\n",
                     leftIndex, rightIndex, mask(leftPrepared.poses), mask(rightPrepared.poses));
    }
    const bool leftSamples = hasDistinctTemporalSamples(left, a, params);
    const bool rightSamples = hasDistinctTemporalSamples(right, b, params);
    const bool leftSupport = hasTemporalSupport(left, params);
    const bool rightSupport = hasTemporalSupport(right, params);
    // Constant velocity is a valid motion. Descriptor novelty would reject
    // it precisely because successive velocity estimates correctly agree.
    // Actual displacement is checked by the trajectory-range gate instead.
    const bool leftDiversity = true;
    const bool rightDiversity = true;
    const bool temporalRun = staticPair || hasTemporalRun(a, b, left, right, params);
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
    const auto alignment = staticPair ? TemporalAlignment{}
        : alignTemporal(a, b, params.temporalSimilarityThreshold);
    std::vector<Descriptor> alignedA, alignedB;
    for (const auto& [i, j] : alignment.path) {
        alignedA.push_back(a[i]); alignedB.push_back(b[j]);
    }
    // Score the supported trajectory, not the original window diagonal:
    // alignment has already found where the repeated gesture occurs.
    const auto& scoreA = staticPair ? a : alignedA;
    const auto& scoreB = staticPair ? b : alignedB;
    if (!staticPair && activeBodyAgreement(scoreA, scoreB) < 0.35) return result;
    const std::size_t rows = scoreA.size(), cols = scoreB.size();
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
            const double cost = std::max(0.0, frameDistance(scoreA[i - 1], scoreB[j - 1])
                - std::min(noise, 0.02));
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
    if (!staticPair && !alignment.path.empty()) {
        result.leftStartSeconds = left.frames[alignment.startLeft].timestampSeconds;
        result.leftEndSeconds = left.frames[alignment.endLeft].timestampSeconds;
        result.rightStartSeconds = right.frames[alignment.startRight].timestampSeconds;
        result.rightEndSeconds = right.frames[alignment.endRight].timestampSeconds;
        result.durationSeconds = std::min(result.leftEndSeconds - result.leftStartSeconds,
                                          result.rightEndSeconds - result.rightStartSeconds);
    }
    const double leftDuration = result.leftEndSeconds - result.leftStartSeconds;
    const double rightDuration = result.rightEndSeconds - result.rightStartSeconds;
    const double durationDenominator = std::max({leftDuration, rightDuration, 1e-9});
    const double timePenalty = params.timeWeight
        * std::abs(leftDuration - rightDuration) / durationDenominator;
    const double dtwScore = std::clamp(std::exp(-4.0 * result.dtwDistance), 0.0, 1.0);
    const double temporalScore = staticPair ? dtwScore : alignment.averageSimilarity;
    const double directionScore = staticPair ? 1.0 : velocityDirectionScore(alignedA, alignedB);
    // Opposite-direction trajectories can have an excellent DTW distance
    // because their amplitudes are similar. Require directional agreement
    // before accepting the movement as the same gesture.
    if (!staticPair && directionScore < 0.45) {
        result.similarity = 0.0;
        return result;
    }
    const double anatomyScore = shapeSimilarity(leftPrepared.poses, rightPrepared.poses);
    // Context is a sanity gate, not a replacement for motion. It suppresses
    // the common false-positive class where the same/front-facing actor is
    // present but the actual shot composition and trajectory do not agree.
    if (!staticPair && result.sceneSimilarity < 0.16
        && (anatomyScore < 0.72 || temporalScore < 0.72 || dtwScore < 0.72)) {
        result.similarity = 0.0;
        return result;
    }
    if (staticPair && std::getenv("PF_DEBUG_MATCHER") != nullptr) {
        std::fprintf(stderr, "PF_DEBUG_MATCHER static-score a=%zu b=%zu dtw=%.4f anatomy=%.4f\n",
                     leftIndex, rightIndex, dtwScore, anatomyScore);
    }
    if (staticPair) {
        // Pose agreement and proportions are complementary observations of
        // the same geometry, not independent probabilities. Use their
        // geometric mean; do not add a fictitious perfect direction score.
        double poseScore = std::sqrt(dtwScore * anatomyScore);
        const double articulation = identity.verified
            ? articulationSimilarity(leftPrepared.articulation, rightPrepared.articulation,
                                     leftPrepared.poses, rightPrepared.poses) : 0.0;
        if (std::getenv("PF_DEBUG_MATCHER") != nullptr)
            std::fprintf(stderr, "PF_DEBUG_MATCHER articulation a=%zu b=%zu score=%.4f\n",
                leftIndex, rightIndex, articulation);
        poseScore = std::max(poseScore, articulation);
        // Background colour is displayed separately. Different lighting or
        // locations cannot make an independently verified pose less similar.
        result.similarity = poseScore >= params.staticPoseSimilarityThreshold
            ? std::clamp(poseScore - timePenalty, 0.0, 0.994) : 0.0;
        return result;
    }
    // The number shown to the editor is *motion* similarity. Body-ReID is a
    // required identity filter, not an extra 40 points for "same actor".
    // A weak component must visibly lower the score rather than being hidden
    // by two stronger averages.
    // Anatomy is deliberately not part of the primary score: the same
    // gesture can start from different neutral poses. It remains a small
    // sanity penalty below, while DTW and frame-wise trajectory agreement
    // decide whether the movement itself repeats.
    const double weightedMotion = 0.60 * dtwScore + 0.25 * temporalScore
        + 0.15 * directionScore;
    const double weakestMotionSignal = std::min({dtwScore, temporalScore, directionScore});
    double calibrated = 0.60 * weakestMotionSignal + 0.40 * weightedMotion;
    calibrated -= 0.10 * std::max(0.0, 0.65 - anatomyScore);
    if (result.sceneSimilarity < 0.35)
        calibrated -= 0.22 * (0.35 - result.sceneSimilarity) / 0.35;
    calibrated -= timePenalty;
    // Reserve the 95%+ band for agreement across all three signals.  A pair
    // with a good average but a weak temporal or anatomical component is a
    // candidate, never an "almost identical" movement.
    if (calibrated > 0.95
        && (dtwScore < 0.96 || temporalScore < 0.96 || anatomyScore < 0.96)) {
        calibrated = 0.949;
    }
    result.similarity = std::clamp(calibrated, 0.0, 0.994);
    if (std::getenv("PF_DEBUG_MATCHER") != nullptr && result.similarity >= params.similarityThreshold)
        std::fprintf(stderr, "PF_DEBUG_MOTION_SCORE a=%zu b=%zu dtw=%.4f temporal=%.4f direction=%.4f anatomy=%.4f active=%.4f score=%.4f\n",
            leftIndex, rightIndex, dtwScore, temporalScore, directionScore, anatomyScore,
            activeBodyAgreement(scoreA, scoreB), result.similarity);
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
        || !(params.staticPoseSimilarityThreshold >= 0.0
             && params.staticPoseSimilarityThreshold <= 1.0)
        || params.minTemporalFrames < 3
        || params.minTemporalDurationSec < 0.0
        || params.sameSourceGapFloorSec < 0.0
        || !std::isfinite(params.sameSceneContextGapSec) || params.sameSceneContextGapSec < 0.0
        || !(params.sameSceneContextThreshold > 0.0 && params.sameSceneContextThreshold <= 1.0)
        || !(params.nmsOverlapThreshold >= 0.0 && params.nmsOverlapThreshold <= 1.0))
        throw std::invalid_argument("MotionMatcher: invalid temporal parameters");
    if (!(params.minAppearanceSimilarity >= -1.0
          && params.minAppearanceSimilarity <= 1.0)
        || !(params.appearanceWeight >= 0.0 && params.appearanceWeight <= 1.0)
        || !(params.minAppearanceEvidence >= 0.0 && params.minAppearanceEvidence <= 1.0)
        || !(params.minFaceSimilarity >= 0.0 && params.minFaceSimilarity <= 1.0))
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
    PreparedWindow leftPrepared;
    leftPrepared.articulation = articulationPoses(left);
    leftPrepared.poses = leftPoses;
    leftPrepared.descriptors = describe(leftPoses, left);
    leftPrepared.motionDelta = leftActivity.meanDelta;
    leftPrepared.activeTransitionRatio = leftActivity.activeTransitionRatio;
    leftPrepared.trajectoryRange = leftActivity.trajectoryRange;
    PreparedWindow rightPrepared;
    rightPrepared.articulation = articulationPoses(right);
    rightPrepared.poses = rightPoses;
    rightPrepared.descriptors = describe(rightPoses, right);
    rightPrepared.motionDelta = rightActivity.meanDelta;
    rightPrepared.activeTransitionRatio = rightActivity.activeTransitionRatio;
    rightPrepared.trajectoryRange = rightActivity.trajectoryRange;

    MotionMatch best = comparePrepared(left, right, leftPrepared, rightPrepared,
                                       params_, leftIndex, rightIndex);
    if (params_.mirrorInvariant) {
        const MotionWindow mirroredSource = mirroredWindow(right);
        rightPrepared.mirroredPoses = normalizePoses(mirroredSource, params_.normalizeSize);
        rightPrepared.mirroredDescriptors = describe(rightPrepared.mirroredPoses, mirroredSource);
        PreparedWindow mirroredRight = rightPrepared;
        mirroredRight.articulation = articulationPoses(mirroredSource);
        mirroredRight.poses = mirroredRight.mirroredPoses;
        mirroredRight.descriptors = mirroredRight.mirroredDescriptors;
        const MotionMatch mirrored = comparePrepared(left, right, leftPrepared, mirroredRight,
                                                     params_, leftIndex, rightIndex);
        if (mirrored.similarity > best.similarity) best = mirrored;
    }
    return best;
}

std::vector<MotionMatch> MotionMatcher::findAllPairs(const std::vector<MotionWindow>& windows) const
{
    std::vector<MotionMatch> matches;
    if (windows.size() < 2) return matches;
    matches.reserve(std::min(params_.maxUniqueResults, windows.size()));

    std::vector<PreparedWindow> prepared(windows.size());
    std::size_t preparedCount = 0;
    std::size_t preparedMotion = 0;
    std::size_t preparedStatic = 0;
    std::size_t embeddingDimension = 0;
    for (std::size_t index = 0; index < windows.size(); ++index) {
        if (windows[index].frames.empty()) continue;
        const auto poses = normalizePoses(windows[index], params_.normalizeSize);
        prepared[index].poses = poses;
        prepared[index].articulation = articulationPoses(windows[index]);
        prepared[index].descriptors = describe(poses, windows[index]);
        if (params_.mirrorInvariant) {
            const MotionWindow mirroredSource = mirroredWindow(windows[index]);
            prepared[index].mirroredPoses = normalizePoses(mirroredSource, params_.normalizeSize);
            prepared[index].mirroredArticulation = articulationPoses(mirroredSource);
            prepared[index].mirroredDescriptors = describe(prepared[index].mirroredPoses, mirroredSource);
        }
        const MotionActivity activity = motionActivity(windows[index], poses);
        prepared[index].motionDelta = activity.meanDelta;
        prepared[index].activeTransitionRatio = activity.activeTransitionRatio;
        prepared[index].trajectoryRange = activity.trajectoryRange;
        const bool staticWindow = windows[index].staticFrameSet;
        if ((staticWindow && !params_.allowStaticFrames)
            || !hasDistinctTemporalSamples(windows[index], prepared[index].descriptors, params_)
            || (!staticWindow && (prepared[index].motionDelta < params_.motionDeltaThreshold
                                  || prepared[index].activeTransitionRatio < params_.minActiveTransitionRatio
                                  || prepared[index].trajectoryRange < params_.minMotionRange))
            || !hasTemporalSupport(windows[index], params_)) {
            if (std::getenv("PF_DEBUG_MATCHER") != nullptr) {
                std::fprintf(stderr,
                    "PF_DEBUG_MATCHER filtered-window id=%zu t=%.3f frames=%zu reid=%.3f samples=%d diversity=%d delta=%.4f range=%.4f active=%.3f\n",
                    index, windows[index].frames.front().timestampSeconds, windows[index].frames.size(),
                    windows[index].appearanceConfidence,
                    hasDistinctTemporalSamples(windows[index], prepared[index].descriptors, params_) ? 1 : 0,
                    hasTemporalDiversity(prepared[index].descriptors) ? 1 : 0,
                    activity.meanDelta, activity.trajectoryRange, activity.activeTransitionRatio);
            }
            prepared[index].descriptors.clear();
            continue;
        }
        ++preparedCount;
        if (staticWindow) ++preparedStatic;
        else {
            ++preparedMotion;
            if (std::getenv("PF_DEBUG_MATCHER") != nullptr) {
                std::fprintf(stderr,
                             "PF_DEBUG_MATCHER motion-window id=%zu t=%.3f track=%zu scene=%zu reid=%.3f delta=%.4f range=%.4f active=%.3f\n",
                             index,
                             windows[index].frames.empty() ? -1.0
                                 : windows[index].frames.front().timestampSeconds,
                             windows[index].trackId, windows[index].sceneIndex,
                             windows[index].appearanceConfidence,
                             prepared[index].motionDelta, prepared[index].trajectoryRange,
                             prepared[index].activeTransitionRatio);
            }
        }
        for (const auto& descriptor : prepared[index].descriptors)
            embeddingDimension = std::max(embeddingDimension, descriptor.size());
        for (const auto& descriptor : prepared[index].mirroredDescriptors)
            embeddingDimension = std::max(embeddingDimension, descriptor.size());
    }
    if (embeddingDimension == 0) return matches;

    // HNSW returns a bounded candidate set. The union of both query
    // directions keeps the all-pairs semantics while making DTW the expensive
    // second pass rather than the first operation on every pair. Do not put
    // static-pose and motion windows in the same nearest-neighbour index:
    // near-static silhouettes otherwise crowd the bounded list and prevent
    // motion candidates from ever reaching the temporal matcher.
    std::unordered_set<std::uint64_t> candidatePairs;
    candidatePairs.reserve(preparedCount * 24U);
    const auto sameKnownShot = [&](std::size_t i, std::size_t j) {
        return windows[i].sourceId == windows[j].sourceId
            && windows[i].hasSceneIndex && windows[j].hasSceneIndex
            && windows[i].sceneIndex == windows[j].sceneIndex;
    };
    for (const bool staticWindow : {false, true}) {
        std::vector<std::size_t> group;
        group.reserve(preparedCount);
        MotionIndex index;
        for (std::size_t windowIndex = 0; windowIndex < prepared.size(); ++windowIndex) {
            auto& item = prepared[windowIndex];
            if (item.descriptors.empty() || windows[windowIndex].staticFrameSet != staticWindow)
                continue;
            item.embedding = embedding(item.descriptors, embeddingDimension);
            if (params_.mirrorInvariant)
                item.mirroredEmbedding = embedding(item.mirroredDescriptors, embeddingDimension);
            index.add(windowIndex, item.embedding);
            group.push_back(windowIndex);
        }
        if (group.size() < 2) continue;
        index.build();
        const std::size_t candidateCount = std::min<std::size_t>(group.size(),
            std::max<std::size_t>(24, std::min<std::size_t>(96, group.size())));
        const double retrievalThreshold = params_.candidateThreshold * 0.65;
        for (const std::size_t i : group) {
            const auto collect = [&](const std::vector<double>& queryEmbedding) {
                // Sliding windows from one long shot can occupy all 96 ANN
                // slots. They are rejected later, hiding a valid other shot.
                // Grow retrieval only when needed, counting usable neighbours
                // rather than same-shot observations. Keep expensive DTW bounded.
                const auto limit = std::min(group.size(), candidateCount * 8);
                for (auto requested = candidateCount;; requested = std::min(limit, requested * 2)) {
                    std::size_t usable = 0;
                    for (const auto neighbour : index.query(queryEmbedding, requested, requested * 2)) {
                        if (neighbour.id == i || sameKnownShot(i, neighbour.id)
                            || neighbour.similarity + 1e-9 < retrievalThreshold) continue;
                        const auto left = std::min(i, neighbour.id);
                        const auto right = std::max(i, neighbour.id);
                        candidatePairs.insert((static_cast<std::uint64_t>(left) << 32U)
                                              | static_cast<std::uint64_t>(right));
                        if (++usable >= candidateCount) break;
                    }
                    if (usable >= candidateCount || requested >= limit) break;
                }
            };
            collect(prepared[i].embedding);
            if (params_.mirrorInvariant && !prepared[i].mirroredEmbedding.empty())
                collect(prepared[i].mirroredEmbedding);
        }
    }

    // Pose retrieval is intentionally broad, but it still misses the same
    // actor when a cut changes camera angle or gesture. Use bounded ANN
    // retrieval for body/face embeddings as a second channel. Embeddings are
    // L2-normalized by their estimators, so Euclidean ANN preserves cosine
    // neighbourhood ordering without an O(N^2) scan.
    std::size_t appearanceCandidatePairs = 0;
    constexpr std::size_t maxAppearanceNeighbours = 64;
    auto addAppearanceCandidates = [&](bool useFace, bool staticWindow) {
        MotionIndex identityIndex;
        std::vector<std::size_t> group;
        for (std::size_t i = 0; i < prepared.size(); ++i) {
            if (prepared[i].descriptors.empty() || windows[i].staticFrameSet != staticWindow) continue;
            const auto& source = useFace ? windows[i].faceEmbedding : windows[i].appearanceEmbedding;
            const double confidence = useFace ? windows[i].faceConfidence : windows[i].appearanceConfidence;
            if (source.empty() || confidence < params_.minAppearanceEvidence) continue;
            std::vector<double> vector(source.begin(), source.end());
            identityIndex.add(i, std::move(vector));
            group.push_back(i);
        }
        if (group.size() < 2) return;
        identityIndex.build();
        const std::size_t count = std::min<std::size_t>(maxAppearanceNeighbours + 1, group.size());
        for (const std::size_t i : group) {
            const auto& source = useFace ? windows[i].faceEmbedding : windows[i].appearanceEmbedding;
            std::vector<double> query(source.begin(), source.end());
            const auto limit = std::min(group.size(), count * 8);
            for (auto requested = count;; requested = std::min(limit, requested * 2)) {
                std::size_t usable = 0;
                for (const auto neighbour : identityIndex.query(query, requested, requested * 2)) {
                    const std::size_t j = neighbour.id;
                    if (i == j || j >= windows.size() || sameKnownShot(i, j)) continue;
                    if (!identityEvidence(windows[i], windows[j], params_).verified) continue;
                    const auto left = std::min(i, j);
                    const auto right = std::max(i, j);
                    if (candidatePairs.insert((static_cast<std::uint64_t>(left) << 32U)
                                              | static_cast<std::uint64_t>(right)).second)
                        ++appearanceCandidatePairs;
                    if (++usable >= maxAppearanceNeighbours) break;
                }
                if (usable >= maxAppearanceNeighbours || requested >= limit) break;
            }
        }
    };
    for (const bool staticWindow : {false, true}) {
        addAppearanceCandidates(false, staticWindow);
        addAppearanceCandidates(true, staticWindow);
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
            const bool sameAppearance = identityEvidence(windows[i], windows[j], params_).verified;
            if (!sameAppearance) {
                if (std::getenv("PF_DEBUG_MATCHER") != nullptr && !windows[i].staticFrameSet) {
                    std::fprintf(stderr,
                                 "PF_DEBUG_MATCHER reject-motion-identity a=%zu b=%zu t=%.3f/%.3f reid=%.3f/%.3f cosine=%.3f\n",
                                 i, j,
                                 windows[i].frames.empty() ? -1.0 : windows[i].frames.front().timestampSeconds,
                                 windows[j].frames.empty() ? -1.0 : windows[j].frames.front().timestampSeconds,
                                 windows[i].appearanceConfidence, windows[j].appearanceConfidence,
                                 appearanceCosine(windows[i].appearanceEmbedding,
                                                  windows[j].appearanceEmbedding));
                }
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
            ? (separateVerifiedShots(windows[i], windows[j], params_) ? 0.0
               : std::max({params_.sameSourceGapFloorSec, params_.sameFileGapSec,
                           params_.minRepeatGapSec}))
            : params_.crossFileGapSec;
        if (gap < requiredGap) continue;
        ++gapPassed;
        // The index is a retrieval stage, not the score shown to the user.
        // Keep its recall broad, then let DTW plus the continuous temporal
        // run make the final acceptance decision.
        const bool identityCandidate = identityEvidence(windows[i], windows[j], params_).verified;
        double coarse = coarseSimilarity(prepared[i].descriptors, prepared[j].descriptors);
        if (params_.mirrorInvariant && !prepared[j].mirroredDescriptors.empty())
            coarse = std::max(coarse, coarseSimilarity(prepared[i].descriptors,
                                                       prepared[j].mirroredDescriptors));
        if (!identityCandidate && coarse < params_.candidateThreshold * 0.65) continue;
        ++coarsePassed;
        ++compared;
        MotionMatch candidate = comparePrepared(windows[i], windows[j], prepared[i], prepared[j],
                                                params_, i, j);
        if (params_.mirrorInvariant && !prepared[j].mirroredDescriptors.empty()) {
            PreparedWindow mirrored = prepared[j];
            mirrored.articulation = mirrored.mirroredArticulation;
            mirrored.poses = mirrored.mirroredPoses;
            mirrored.descriptors = mirrored.mirroredDescriptors;
            mirrored.embedding = mirrored.mirroredEmbedding;
            const MotionMatch mirroredCandidate = comparePrepared(windows[i], windows[j],
                                                                  prepared[i], mirrored, params_, i, j);
            if (mirroredCandidate.similarity > candidate.similarity) candidate = mirroredCandidate;
        }
        if (std::getenv("PF_DEBUG_MATCHER") != nullptr) {
            std::fprintf(stderr, "PF_DEBUG_MATCHER compared a=%zu b=%zu t=%.3f/%.3f static=%d/%d similarity=%.3f dtw=%.3f appearance=%.3f\n",
                         i, j,
                         windows[i].frames.empty() ? -1.0 : windows[i].frames.front().timestampSeconds,
                         windows[j].frames.empty() ? -1.0 : windows[j].frames.front().timestampSeconds,
                         windows[i].staticFrameSet ? 1 : 0, windows[j].staticFrameSet ? 1 : 0,
                         candidate.similarity, candidate.dtwDistance,
                         candidate.appearanceSimilarity);
        }
        if (candidate.similarity >= params_.similarityThreshold) matches.push_back(candidate);
    }
    const auto strongestFirst = [](const auto& a, const auto& b) {
        if (std::abs(a.similarity - b.similarity) > 1e-12) return a.similarity > b.similarity;
        if (a.leftIndex != b.leftIndex) return a.leftIndex < b.leftIndex;
        return a.rightIndex < b.rightIndex;
    };
    std::sort(matches.begin(), matches.end(), strongestFirst);
    // Keep the strongest result for overlapping windows.  A window may still
    // participate in multiple independent pairs; only near-identical pairs
    // are removed, which is the semantics of duplicateWindowSec.
    std::vector<MotionMatch> unique;
    unique.reserve(matches.size());
    std::size_t motionResults = 0;
    std::size_t staticResults = 0;
    // Head geometry and body articulation have different score distributions.
    // Preserve both observable regions within the SAME static result budget:
    // balance accepted (not merely visited) results, lending unused slots to
    // the other region. Thresholds, identity gates and motion budget stay intact.
    std::vector<const MotionMatch*> heads, bodies;
    for (const auto& match : matches) {
        if (windows[match.leftIndex].staticFrameSet)
            (match.headOnlyComparison ? heads : bodies).push_back(&match);
    }
    std::size_t nextHead = 0, nextBody = 0, keptHeads = 0, keptBodies = 0;
    for (const MotionMatch& ranked : matches) {
        const MotionMatch* next = &ranked;
        if (windows[ranked.leftIndex].staticFrameSet) {
            const bool takeHead = nextHead < heads.size()
                && (nextBody == bodies.size() || keptHeads < keptBodies
                    || (keptHeads == keptBodies
                        && strongestFirst(*heads[nextHead], *bodies[nextBody])));
            next = takeHead ? heads[nextHead++] : bodies[nextBody++];
        }
        const MotionMatch& candidate = *next;
        const bool staticCandidate = windows[candidate.leftIndex].staticFrameSet;
        auto& typeCount = staticCandidate ? staticResults : motionResults;
        if (std::getenv("PF_DEBUG_SELECTION") != nullptr) {
            std::fprintf(stderr, "PF_SELECTION t=%.3f/%.3f score=%.6f static=%d head=%d used=%zu limit=%zu\n",
                candidate.leftStartSeconds, candidate.rightStartSeconds, candidate.similarity,
                staticCandidate ? 1 : 0, candidate.headOnlyComparison ? 1 : 0,
                typeCount, params_.maxUniqueResults);
        }
        if (typeCount >= params_.maxUniqueResults) continue;
        const bool duplicate = std::any_of(unique.begin(), unique.end(), [&](const MotionMatch& kept) {
            // A pose result is not a duplicate of a repeated movement. Their
            // scores measure different things and cannot compete in NMS.
            if (staticCandidate != windows[kept.leftIndex].staticFrameSet) return false;
            const bool sameOrientation = windows[candidate.leftIndex].sourceId == windows[kept.leftIndex].sourceId
                && windows[candidate.rightIndex].sourceId == windows[kept.rightIndex].sourceId;
            const bool swappedOrientation = windows[candidate.leftIndex].sourceId == windows[kept.rightIndex].sourceId
                && windows[candidate.rightIndex].sourceId == windows[kept.leftIndex].sourceId;
            if (!sameOrientation && !swappedOrientation) return false;
            const auto identitiesCompatible = [&](std::size_t candidateIndex, std::size_t keptIndex) {
                const auto& candidateWindow = windows[candidateIndex];
                const auto& keptWindow = windows[keptIndex];
                const auto evidence = identityEvidence(candidateWindow, keptWindow, params_);
                // A track change alone is inconclusive. Independent, confident
                // appearance disagreement means these are different people,
                // even when a scene index and timestamps happen to coincide.
                return !evidence.available || evidence.verified;
            };
            const bool compatible = sameOrientation
                && identitiesCompatible(candidate.leftIndex, kept.leftIndex)
                && identitiesCompatible(candidate.rightIndex, kept.rightIndex);
            const bool reverseCompatible = swappedOrientation
                && identitiesCompatible(candidate.leftIndex, kept.rightIndex)
                && identitiesCompatible(candidate.rightIndex, kept.leftIndex);
            if (!compatible && !reverseCompatible) return false;
            const bool knownScenes = windows[candidate.leftIndex].hasSceneIndex
                && windows[candidate.rightIndex].hasSceneIndex
                && windows[kept.leftIndex].hasSceneIndex && windows[kept.rightIndex].hasSceneIndex;
            if (knownScenes) {
                const bool directScenes = sameOrientation
                    && windows[candidate.leftIndex].sceneIndex == windows[kept.leftIndex].sceneIndex
                    && windows[candidate.rightIndex].sceneIndex == windows[kept.rightIndex].sceneIndex;
                const bool reverseScenes = swappedOrientation
                    && windows[candidate.leftIndex].sceneIndex == windows[kept.rightIndex].sceneIndex
                    && windows[candidate.rightIndex].sceneIndex == windows[kept.leftIndex].sceneIndex;
                // Distinct edited shots can be only a second apart. A/B and
                // A/C are different pairs; timestamp proximity must not erase
                // C merely because the montage has rapid cuts.
                if (!directScenes && !reverseScenes) return false;
            }
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
            if (swappedOrientation) {
                const bool reverseOverlap = overlapRatio(candidate.leftStartSeconds, candidate.leftEndSeconds,
                    kept.rightStartSeconds, kept.rightEndSeconds) >= params_.nmsOverlapThreshold
                    && overlapRatio(candidate.rightStartSeconds, candidate.rightEndSeconds,
                        kept.leftStartSeconds, kept.leftEndSeconds) >= params_.nmsOverlapThreshold;
                const bool reverseNear = std::abs(candidate.leftStartSeconds - kept.rightStartSeconds) <= params_.duplicateWindowSec
                    && std::abs(candidate.rightStartSeconds - kept.leftStartSeconds) <= params_.duplicateWindowSec;
                const bool reverseScene = windows[candidate.leftIndex].hasSceneIndex
                    && windows[candidate.rightIndex].hasSceneIndex
                    && windows[kept.leftIndex].hasSceneIndex && windows[kept.rightIndex].hasSceneIndex
                    && windows[candidate.leftIndex].sceneIndex == windows[kept.rightIndex].sceneIndex
                    && windows[candidate.rightIndex].sceneIndex == windows[kept.leftIndex].sceneIndex;
                if (reverseOverlap || reverseNear || reverseScene) return true;
            }
            // Without aligned source IDs, direct timestamps are not comparable.
            if (!sameOrientation) return false;
            const bool sameTrackAndScene = windows[candidate.leftIndex].trackId != 0
                && windows[candidate.leftIndex].trackId == windows[kept.leftIndex].trackId
                && windows[candidate.rightIndex].trackId != 0
                && windows[candidate.rightIndex].trackId == windows[kept.rightIndex].trackId
                && windows[candidate.leftIndex].hasSceneIndex
                && windows[candidate.rightIndex].hasSceneIndex
                && windows[kept.leftIndex].hasSceneIndex && windows[kept.rightIndex].hasSceneIndex
                && windows[candidate.leftIndex].sceneIndex == windows[kept.leftIndex].sceneIndex
                && windows[candidate.rightIndex].sceneIndex == windows[kept.rightIndex].sceneIndex;
            // Track IDs can restart after an occlusion. Scene provenance is
            // the stable unit represented by a result card, so collapse
            // windows from the same source/scene pair even when IDs differ.
            const bool sameSourceAndScene = windows[candidate.leftIndex].sourceId
                    == windows[kept.leftIndex].sourceId
                && windows[candidate.rightIndex].sourceId
                    == windows[kept.rightIndex].sourceId
                && windows[candidate.leftIndex].hasSceneIndex
                && windows[candidate.rightIndex].hasSceneIndex
                && windows[kept.leftIndex].hasSceneIndex && windows[kept.rightIndex].hasSceneIndex
                && windows[candidate.leftIndex].sceneIndex
                    == windows[kept.leftIndex].sceneIndex
                && windows[candidate.rightIndex].sceneIndex
                    == windows[kept.rightIndex].sceneIndex;
            // A scene is the unit shown in the results rail. Sliding motion
            // windows inside the same tracked shot must not produce a second
            // card for that shot pair (the old 1.25 s bound let exactly this
            // duplicate through when the windows were five seconds apart).
            // Keep the strongest window selected by the sort above.
            const bool closeInTime = leftDelta <= std::max(2.0, params_.duplicateWindowSec * 2.0)
                && rightDelta <= std::max(2.0, params_.duplicateWindowSec * 2.0);
            const bool sameSceneNeighborhood = windows[candidate.leftIndex].sourceId
                    == windows[kept.leftIndex].sourceId
                && windows[candidate.rightIndex].sourceId
                    == windows[kept.rightIndex].sourceId
                && std::abs(candidate.leftSceneStartSeconds - kept.leftSceneStartSeconds) <= 2.0
                && std::abs(candidate.rightSceneStartSeconds - kept.rightSceneStartSeconds) <= 2.0
                && sceneContextSimilarity(windows[candidate.leftIndex], windows[kept.leftIndex]) >= 0.84
                && sceneContextSimilarity(windows[candidate.rightIndex], windows[kept.rightIndex]) >= 0.84;
            return overlapping || (leftDelta <= params_.duplicateWindowSec
                                   && rightDelta <= params_.duplicateWindowSec)
                || sameTrackAndScene || sameSourceAndScene
                || (closeInTime && sameSceneNeighborhood);
        });
        if (!duplicate) {
            unique.push_back(candidate);
            ++typeCount;
            if (staticCandidate) ++(candidate.headOnlyComparison ? keptHeads : keptBodies);
        }
    }
    // Retrieval policy must not change the public descending-score ordering.
    std::sort(unique.begin(), unique.end(), strongestFirst);
    matches = std::move(unique);
    if (std::getenv("PF_DEBUG_MATCHER") != nullptr) {
        std::fprintf(stderr,
                     "PF_DEBUG_MATCHER windows=%zu prepared=%zu motion=%zu static=%zu candidates=%zu appearanceCandidates=%zu trackRejected=%zu sceneRejected=%zu staticRejected=%zu gapPassed=%zu coarsePassed=%zu compared=%zu accepted=%zu\n",
                     windows.size(), preparedCount, preparedMotion, preparedStatic, candidatePairs.size(), appearanceCandidatePairs, trackRejected,
                     sceneRejected, staticRejected, gapPassed, coarsePassed, compared,
                     matches.size());
    }
    return matches;
}

} // namespace pfcore
