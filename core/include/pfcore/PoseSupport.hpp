#pragma once
#include "pfcore/MotionMatcher.hpp"
#include <cmath>
#include <vector>

namespace pfcore {
struct PoseSampleRange { std::size_t begin = 0, end = 0; }; // inclusive endpoints

// Extra held-pose candidates come from contiguous observed limbs, never a
// lucky frame or interpolated hidden hand. This reuses existing detections.
inline std::vector<PoseSampleRange> observedPoseRuns(const MotionWindow& window)
{
    std::vector<PoseSampleRange> result;
    std::size_t begin = 0;
    unsigned common = 0;
    const auto finish = [&](std::size_t end) {
        if (common && end - begin + 1 >= MotionMatcherParams::minimumStaticSamples
            && window.frames[end].timestampSeconds - window.frames[begin].timestampSeconds
                + 1e-9 >= MotionMatcherParams::minimumStaticSpanSeconds)
            result.push_back({begin, end});
    };
    for (std::size_t i = 0; i < window.frames.size(); ++i) {
        const auto& points = window.frames[i].keypoints;
        const auto visible = [&](std::size_t j) {
            return std::isfinite(window.frames[i].timestampSeconds)
                && points.size() == 17 && std::isfinite(points[j].x)
                && std::isfinite(points[j].y) && std::isfinite(points[j].confidence)
                && points[j].confidence >= 0.5;
        };
        unsigned mask = 0;
        if (visible(0) && visible(5) && visible(6) && visible(9) && visible(10)) {
            if (visible(7)) mask |= 1;
            if (visible(8)) mask |= 2;
        }
        if (visible(0) && visible(11) && visible(12)) {
            if (visible(13) && visible(15)) mask |= 4;
            if (visible(14) && visible(16)) mask |= 8;
        }
        const bool gap = i && (!std::isfinite(window.frames[i-1].timestampSeconds)
            || window.frames[i].timestampSeconds <= window.frames[i-1].timestampSeconds
            || window.frames[i].timestampSeconds - window.frames[i-1].timestampSeconds > 0.5);
        if (common && (gap || !(common & mask))) {
            finish(i-1);
            common = 0;
        }
        if (mask) {
            if (!common) { begin = i; common = mask; }
            else common &= mask;
        }
    }
    if (common && !window.frames.empty()) finish(window.frames.size()-1);
    return result;
}
} // namespace pfcore
