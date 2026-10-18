#pragma once

#include <algorithm>
#include <cmath>

namespace pfcore {

struct ParallelClipRange {
    double start = 0;
    double end = 0;
};

// Track observations can end before a shot does. Use the enclosing shot
// boundary for export refinement; the observation end only limits pose data.
inline double sceneBoundaryEnd(double sceneStart, double sceneEnd, double fallbackEnd)
{
    if (std::isfinite(sceneStart) && sceneStart >= 0.0
        && std::isfinite(sceneEnd) && sceneEnd > sceneStart) return sceneEnd;
    return fallbackEnd;
}

inline bool sceneBoundaryMayNeedRefinement(double clipStart, double clipEnd,
                                           double sceneEnd, double toleranceSeconds = 0.35)
{
    return std::isfinite(clipStart) && std::isfinite(clipEnd) && std::isfinite(sceneEnd)
        && std::isfinite(toleranceSeconds) && toleranceSeconds >= 0.0
        && clipEnd > clipStart && sceneEnd > clipStart
        && sceneEnd <= clipEnd + toleranceSeconds;
}

// Export only the supported match. Do not pad a short parallel with unrelated
// seconds or truncate a long one. A single sampled pose gets one frame.
inline ParallelClipRange parallelClipRange(double matchStart, double matchEnd,
                                           double sceneStart, double sceneEnd,
                                           double framesPerSecond=24.0)
{
    if (!std::isfinite(matchStart)) return {};
    matchStart = std::max(0.0, matchStart);
    if (!std::isfinite(matchEnd) || matchEnd < matchStart) matchEnd = matchStart;
    const double lower = std::isfinite(sceneStart) && sceneStart >= 0
        && sceneStart <= matchStart ? sceneStart : 0.0;
    const double frameDuration=std::isfinite(framesPerSecond) && framesPerSecond>0
        ? 1.0/framesPerSecond:1.0/24.0;
    const double duration = matchEnd>matchStart ? matchEnd-matchStart:frameDuration;
    const double upper = std::isfinite(sceneEnd) && sceneEnd > matchStart
        ? sceneEnd : std::max(matchEnd, matchStart + duration);
    const double start = std::max(matchStart, lower);
    return {start, std::min(start + duration, upper)};
}

} // namespace pfcore
