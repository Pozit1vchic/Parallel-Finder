#pragma once

#include <cstddef>

namespace pfservices {

// LRU cache for media thumbnails (spec section 4 / services list: "LRU64").
// Stage 0 stub: capacity accounting only; real LRU map + disk eviction and the
// %LocalAppData%\ParallelFinder\cache\ path land in stage 4.
class ThumbnailCache {
public:
    // "LRU64" — default capacity of 64 entries.
    static constexpr std::size_t kDefaultMaxEntries = 64;

    explicit ThumbnailCache(std::size_t maxEntries = kDefaultMaxEntries);

    // Capacity must be >= 1. Throws std::invalid_argument otherwise.
    void setMaxEntries(std::size_t maxEntries);
    std::size_t maxEntries() const noexcept { return maxEntries_; }

private:
    std::size_t maxEntries_;
};

} // namespace pfservices
