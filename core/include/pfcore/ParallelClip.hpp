#pragma once

#include <algorithm>
#include <cmath>

namespace pfcore {

struct ParallelClipRange {
    double start = 0;
    double end = 0;
};

// Export only the supported match. Do not pad a short parallel with unrelated
// seconds or truncate a long one. A single sampled pose gets one frame.
inline ParallelClipRange parallelClipRange(double matchStart, double matchEnd,
                                           double sceneStart, double sceneEnd)
{
    if (!std::isfinite(matchStart)) return {};
    matchStart = std::max(0.0, matchStart);
    if (!std::isfinite(matchEnd) || matchEnd < matchStart) matchEnd = matchStart;
    const double lower = std::isfinite(sceneStart) && sceneStart >= 0
        && sceneStart <= matchStart ? sceneStart : 0.0;
    const double duration = std::max(1.0 / 24.0, matchEnd - matchStart);
    const double upper = std::isfinite(sceneEnd) && sceneEnd > matchStart
        ? sceneEnd : std::max(matchEnd, matchStart + duration);
    const double start = std::max(matchStart, lower);
    return {start, std::min(start + duration, upper)};
}

} // namespace pfcore
