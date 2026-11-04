#pragma once

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <string>
#include <vector>

namespace pfui {

struct PreviewRequestOrderKey {
    std::string source;
    double seconds = 0.0;
};

// Character groups can be merged after the underlying analysis is cached.
// Candidate IDs and appearance indices then no longer identify a frame. Use
// the exact source timestamp instead; -0 and +0 are the same request.
inline std::string characterPreviewCacheKey(const std::string& artifact, double seconds) {
    const double canonical = seconds == 0.0 ? 0.0 : seconds;
    return artifact + "|character-preview-v3-source-time|" +
        std::to_string(std::bit_cast<std::uint64_t>(canonical));
}

// Keep request storage and result indices intact while rendering previews in
// decoder-friendly source/time order. Callers publish each URL by its original
// request index, independent of the order in which decoding is scheduled.
inline std::vector<std::size_t> previewRenderOrder(
    const std::vector<PreviewRequestOrderKey>& requests) {
    std::vector<std::size_t> order(requests.size());
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        if (requests[a].source != requests[b].source)
            return requests[a].source < requests[b].source;
        return requests[a].seconds < requests[b].seconds;
    });
    return order;
}

} // namespace pfui
