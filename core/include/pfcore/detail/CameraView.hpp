#pragma once

#include <algorithm>
#include <cmath>
#include <span>

namespace pfcore::detail {

inline bool sameCameraPixels(std::span<const float> left, std::span<const float> right)
{
    if (left.size() != 432 || right.size() != left.size()) return false;
    for (std::size_t i = 0; i < left.size(); ++i)
        if (!std::isfinite(left[i]) || !std::isfinite(right[i])
            || left[i] < 0 || right[i] < 0 || left[i] > 1 || right[i] > 1) return false;
    for (int shift = -1; shift <= 1; ++shift) {
        // These are the exact sums of the original half-integer cell weights:
        // nine rows, three channels, 26 columns' weight (24 with a shift).
        const double fullWeight = shift == 0 ? 702.0 : 648.0;
        double difference = 0;
        bool rejected = false;
        for (int row = 0; row < 9; ++row) {
            for (int column = std::max(0, -shift); column < std::min(16, 16 - shift); ++column) {
                const double cellWeight = ((column < 5 || column >= 11 ? 2.0 : 1.0)
                    + (column + shift < 5 || column + shift >= 11 ? 2.0 : 1.0)) / 2.0;
                for (int channel = 0; channel < 3; ++channel)
                    difference += cellWeight * std::abs(left[(row*16+column)*3+channel]
                        - right[(row*16+column+shift)*3+channel]);
            }
            // Every contribution is nonnegative. Once this exact prefix / the
            // original FULL denominator exceeds the threshold, no remaining
            // row can make the original final comparison pass. Preserve the
            // original division (not a rounded multiplied threshold).
            if (difference / fullWeight > .035) { rejected = true; break; }
        }
        if (!rejected && difference / fullWeight <= .035) return true;
    }
    return false;
}
} // namespace pfcore::detail
