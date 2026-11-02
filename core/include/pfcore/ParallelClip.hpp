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

// Refine only an automatic clip that actually begins at its sampled shot
// boundary. A padded long-shot clip and manual edits must retain their start.
inline bool sceneStartMayNeedRefinement(double clipStart, double sceneStart)
{
    return std::isfinite(clipStart) && std::isfinite(sceneStart)
        && sceneStart > 0.0 && std::abs(clipStart-sceneStart) < 1e-6;
}

// Matcher endpoints are timestamps of the first and last supported frames,
// while the exporter treats end as an exclusive boundary. Extend to the middle
// of the final frame's nominal display interval: this keeps that frame without
// reaching the following frame or adding unrelated scene context. A single
// sampled pose gets one frame.
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
    const double frameBoundaryMargin=frameDuration*.5;
    const double duration = matchEnd>matchStart
        ? matchEnd-matchStart+frameBoundaryMargin:frameBoundaryMargin;
    // A scene end at or before the last matched frame is inconsistent with
    // an exclusive shot boundary. Do not let it cut supported frames off.
    const double upper = std::isfinite(sceneEnd) && sceneEnd > matchEnd
        ? sceneEnd : std::max(matchEnd, matchStart + duration);
    const double start = std::max(matchStart, lower);
    return {start, std::min(start + duration, upper)};
}

// Export a complete short shot around a match. For unusually long shots,
// retain the whole match and add bounded context without crossing the cut.
// If the shot bounds are unavailable or inconsistent, keep the supported
// match instead of inventing an unknown scene boundary.
inline ParallelClipRange parallelSceneClipRange(double matchStart, double matchEnd,
                                                double sceneStart, double sceneEnd,
                                                double framesPerSecond=24.0)
{
    const auto match=parallelClipRange(matchStart,matchEnd,sceneStart,sceneEnd,framesPerSecond);
    if(!std::isfinite(matchStart) || !std::isfinite(matchEnd)
        || !std::isfinite(sceneStart) || !std::isfinite(sceneEnd)
        || sceneStart<0 || sceneEnd<=sceneStart || matchEnd<matchStart
        || matchStart<sceneStart || matchEnd>=sceneEnd) return match;
    constexpr double maxShotSeconds=12.0;
    constexpr double contextSeconds=1.25;
    if(sceneEnd-sceneStart<=maxShotSeconds) return {sceneStart,sceneEnd};
    const double budget=std::max(0.0,maxShotSeconds-(match.end-match.start));
    const double padding=std::min(contextSeconds,budget*.5);
    return {std::max(sceneStart,match.start-padding),std::min(sceneEnd,match.end+padding)};
}

} // namespace pfcore
