#pragma once

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <string>
#include <vector>

namespace pfui {

struct PreviewRequestOrderKey {
    std::string source;
    double seconds = 0.0;
};

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
