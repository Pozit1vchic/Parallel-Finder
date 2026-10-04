#pragma once

#include <cmath>
#include <limits>
#include <stdexcept>

namespace pfcore::detail {

// Preserve repeated-addition cadence for ordinary video timestamps. Bound
// catch-up on discontinuities and ensure floating-point addition progresses.
inline bool selectSampleTimestamp(double timestamp, double interval, double& next)
{
    if (!std::isfinite(timestamp))
        throw std::invalid_argument("VideoSampleReader: frame timestamp must be finite");
    const double cutoff = timestamp + 1e-9;
    if (interval <= 0 || cutoff < next) return false;
    if (!std::isfinite(next)) {
        next = timestamp + interval;
        if (!(next > cutoff))
            next = std::nextafter(cutoff, std::numeric_limits<double>::infinity());
        return true;
    }
    // Normal frame gaps take one or a few iterations, with exactly the same
    // additions as before. Huge jumps must not monopolize a decoder worker.
    for (unsigned attempts = 0; attempts < 4096; ++attempts) {
        const double advanced = next + interval;
        if (!(advanced > next)) {
            next = std::nextafter(cutoff, std::numeric_limits<double>::infinity());
            return true;
        }
        next = advanced;
        if (next > cutoff) return true;
    }
    const double steps = std::floor((cutoff - next) / interval) + 1.0;
    const double advanced = std::fma(steps, interval, next);
    next = std::isfinite(advanced) && advanced > cutoff ? advanced
        : std::nextafter(cutoff, std::numeric_limits<double>::infinity());
    return true;
}

} // namespace pfcore::detail
