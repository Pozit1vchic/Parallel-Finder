#pragma once

#include "pfcore/MotionMatcher.hpp"
#include <cmath>
#include <compare>

namespace pfcore::detail {

// Epsilon ties are not transitive. Use exact scores, put NaNs after numeric
// values, and break exact ties by window indices for reproducible selection.
inline std::weak_ordering descendingScoreOrder(double a, double b) noexcept
{
    const bool nanA = std::isnan(a), nanB = std::isnan(b);
    if (nanA || nanB) {
        if (nanA == nanB) return std::weak_ordering::equivalent;
        return nanA ? std::weak_ordering::greater : std::weak_ordering::less;
    }
    if (a == b) return std::weak_ordering::equivalent;
    return a > b ? std::weak_ordering::less : std::weak_ordering::greater;
}

inline bool strongestMatchFirst(const MotionMatch& a, const MotionMatch& b) noexcept
{
    const auto order = descendingScoreOrder(a.similarity, b.similarity);
    if (order != 0) return order < 0;
    if (a.leftIndex != b.leftIndex) return a.leftIndex < b.leftIndex;
    return a.rightIndex < b.rightIndex;
}

inline bool directHeadMatchFirst(const MotionMatch& a, const MotionMatch& b, double threshold) noexcept
{
    const bool directA = a.unmirroredSimilarity >= threshold;
    const bool directB = b.unmirroredSimilarity >= threshold;
    if (directA != directB) return directA;
    if (directA) {
        const auto order = descendingScoreOrder(a.unmirroredSimilarity, b.unmirroredSimilarity);
        if (order != 0) return order < 0;
    }
    return strongestMatchFirst(a, b);
}

} // namespace pfcore::detail
