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
    MotionMatch result {leftIndex, rightIndex, 0.0, std::numeric_limits<double>::infinity(),
                        std::min(duration(left), duration(right))};
    if (a.empty() || b.empty()) return result;
    const std::size_t rows = a.size(), cols = b.size();
    const double inf = std::numeric_limits<double>::infinity();
    std::vector<double> previous(cols + 1, inf), current(cols + 1, inf);
    previous[0] = 0.0;
    const std::size_t band = std::max(params_.dtwBand, rows > cols ? rows - cols : cols - rows);
    for (std::size_t i = 1; i <= rows; ++i) {
        std::fill(current.begin(), current.end(), inf);
        const std::size_t begin = i > band ? i - band : 1;
        const std::size_t end = std::min(cols, i + band);
        for (std::size_t j = begin; j <= end; ++j) {
            const double cost = frameDistance(a[i - 1], b[j - 1]);
            current[j] = cost + std::min({previous[j], current[j - 1], previous[j - 1]});
        }
        previous.swap(current);
    }
    result.dtwDistance = previous[cols] / static_cast<double>(rows + cols);
    const double timePenalty = params_.timeWeight * std::abs(static_cast<double>(rows) - static_cast<double>(cols))
        / static_cast<double>(std::max(rows, cols));
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
            const double gap = std::abs(windows[i].frames.front().timestampSeconds
                - windows[j].frames.front().timestampSeconds);
            const double requiredGap = sameSource ? params_.sameFileGapSec : params_.crossFileGapSec;
            if (sameSource && gap < requiredGap) continue;
            MotionMatch candidate = compare(windows[i], windows[j], i, j);
            if (candidate.similarity >= params_.similarityThreshold) matches.push_back(candidate);
        }
    }
    std::sort(matches.begin(), matches.end(), [](const auto& a, const auto& b) {
        return a.similarity > b.similarity;
    });
    if (matches.size() > params_.maxUniqueResults) matches.resize(params_.maxUniqueResults);
    return matches;
}

} // namespace pfcore
