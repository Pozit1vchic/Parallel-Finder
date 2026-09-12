#include "pfcore/MotionMatcher.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace pfcore {
namespace {

using Descriptor = std::vector<double>;

std::vector<Descriptor> describe(const MotionWindow& window)
{
    std::vector<Descriptor> result;
    for (const PoseFrame& frame : window.frames) {
        if (frame.keypoints.empty()) { result.emplace_back(); continue; }
        double cx = 0.0, cy = 0.0, weight = 0.0;
        for (const auto& point : frame.keypoints) {
            const double w = std::max(0.0, point.confidence);
            cx += point.x * w; cy += point.y * w; weight += w;
        }
        if (weight <= 1e-9) weight = static_cast<double>(frame.keypoints.size());
        cx /= weight; cy /= weight;
        double scale = 0.0;
        for (const auto& point : frame.keypoints)
            scale = std::max(scale, std::hypot(point.x - cx, point.y - cy));
        if (scale <= 1e-9) scale = 1.0;
        Descriptor descriptor;
        descriptor.reserve(frame.keypoints.size() * 2);
        for (const auto& point : frame.keypoints) {
            descriptor.push_back((point.x - cx) / scale);
            descriptor.push_back((point.y - cy) / scale);
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

double duration(const MotionWindow& window)
{
    if (window.frames.size() < 2) return 0.0;
    return std::max(0.0, window.frames.back().timestampSeconds - window.frames.front().timestampSeconds);
}

} // namespace

MotionMatcher::MotionMatcher(MotionMatcherParams params) : params_(params) { setParams(params); }

void MotionMatcher::setParams(MotionMatcherParams params)
{
    if (!(params.similarityThreshold >= 0.0 && params.similarityThreshold <= 1.0)
        || !(params.candidateThreshold >= 0.0 && params.candidateThreshold <= 1.0)
        || params.maxUniqueResults == 0 || params.dtwBand == 0 || params.noiseFactor < 0.0)
        throw std::invalid_argument("MotionMatcher: invalid parameters");
    params_ = params;
}

MotionMatch MotionMatcher::compare(const MotionWindow& left, const MotionWindow& right,
                                   std::size_t leftIndex, std::size_t rightIndex) const
{
    const auto a = describe(left), b = describe(right);
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
    if (a.empty() || b.empty()) return result;
    const std::size_t rows = a.size(), cols = b.size();
    const double noise = params_.noiseFactor * std::max(noiseFloor(a), noiseFloor(b));
    const double inf = std::numeric_limits<double>::infinity();
    std::vector<double> previous(cols + 1, inf), current(cols + 1, inf);
    previous[0] = 0.0;
    const std::size_t band = std::max(params_.dtwBand, rows > cols ? rows - cols : cols - rows);
    for (std::size_t i = 1; i <= rows; ++i) {
        std::fill(current.begin(), current.end(), inf);
        const std::size_t begin = i > band ? i - band : 1;
        const std::size_t end = std::min(cols, i + band);
        for (std::size_t j = begin; j <= end; ++j) {
            const double cost = std::max(0.0, frameDistance(a[i - 1], b[j - 1]) - noise);
            current[j] = cost + std::min({previous[j], current[j - 1], previous[j - 1]});
        }
        previous.swap(current);
    }
    result.dtwDistance = previous[cols] / static_cast<double>(rows + cols);
    const double leftDuration = duration(left);
    const double rightDuration = duration(right);
    const double durationDenominator = std::max({leftDuration, rightDuration, 1e-9});
    const double timePenalty = params_.timeWeight
        * std::abs(leftDuration - rightDuration) / durationDenominator;
    result.similarity = std::clamp(std::exp(-(result.dtwDistance + timePenalty)), 0.0, 1.0);
    return result;
}

std::vector<MotionMatch> MotionMatcher::findAllPairs(const std::vector<MotionWindow>& windows) const
{
    std::vector<MotionMatch> matches;
    for (std::size_t i = 0; i < windows.size(); ++i) {
        if (windows[i].frames.empty()) continue;
        for (std::size_t j = i + 1; j < windows.size(); ++j) {
            if (windows[j].frames.empty()) continue;
            const bool sameSource = windows[i].sourceId == windows[j].sourceId;
            const double leftStart = windows[i].frames.front().timestampSeconds;
            const double rightStart = windows[j].frames.front().timestampSeconds;
            const double gap = std::abs(leftStart - rightStart);
            const double requiredGap = sameSource
                ? std::max(params_.sameFileGapSec, params_.minRepeatGapSec)
                : params_.crossFileGapSec;
            if (sameSource && gap < requiredGap) continue;
            const auto leftDescriptor = describe(windows[i]);
            const auto rightDescriptor = describe(windows[j]);
            if (coarseSimilarity(leftDescriptor, rightDescriptor) < params_.candidateThreshold) continue;
            MotionMatch candidate = compare(windows[i], windows[j], i, j);
            if (candidate.similarity >= params_.similarityThreshold) matches.push_back(candidate);
        }
    }
    std::sort(matches.begin(), matches.end(), [](const auto& a, const auto& b) {
        return a.similarity > b.similarity;
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
            return leftDelta <= params_.duplicateWindowSec && rightDelta <= params_.duplicateWindowSec;
        });
        if (!duplicate) unique.push_back(candidate);
        if (unique.size() >= params_.maxUniqueResults) break;
    }
    matches = std::move(unique);
    return matches;
}

} // namespace pfcore
