#include "pfservices/ThumbnailCache.hpp"

#include <stdexcept>

namespace pfservices {

ThumbnailCache::ThumbnailCache(std::size_t maxEntries)
    : maxEntries_(maxEntries)
{
    if (maxEntries_ == 0) {
        throw std::invalid_argument("ThumbnailCache: capacity must be >= 1");
    }
}

void ThumbnailCache::setMaxEntries(std::size_t maxEntries)
{
    if (maxEntries == 0) {
        throw std::invalid_argument("ThumbnailCache: capacity must be >= 1");
    }
    maxEntries_ = maxEntries;
}

} // namespace pfservices
