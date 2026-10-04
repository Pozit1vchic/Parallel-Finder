#pragma once

#include <algorithm>
#include <cmath>

namespace pfcore {

struct ParallelClipRange {
    double start = 0;
    double end = 0;
};

// Short context around the supported match, not the remainder of the shot.
// Five seconds by default; a longer matched gesture may use up to six.
// Shift context at shot edges before shortening it for a genuinely short shot.
inline ParallelClipRange parallelClipRange(double matchStart, double matchEnd,
                                           double sceneStart, double sceneEnd)
{
    if (!std::isfinite(matchStart)) return {};
    matchStart = std::max(0.0, matchStart);
    if (!std::isfinite(matchEnd) || matchEnd < matchStart) matchEnd = matchStart;
    const double lower = std::isfinite(sceneStart) && sceneStart >= 0
        && sceneStart <= matchStart ? sceneStart : 0.0;
    const double duration = std::clamp(std::max(5.0, matchEnd - matchStart), 4.0, 6.0);
    const double upper = std::isfinite(sceneEnd) && sceneEnd > matchStart
        ? sceneEnd : std::max(matchEnd, matchStart + duration);
    const double length = std::min(duration, upper - lower);
    const double center = (matchStart + std::min(matchEnd, upper)) * .5;
    const double start = std::clamp(center - length * .5, lower, upper - length);
    return {start, start + length};
}

} // namespace pfcore
