#include <gtest/gtest.h>

#include <pfservices/ThumbnailCache.hpp>

#include <stdexcept>

namespace {

TEST(ThumbnailCache, DefaultCapacityIsLru64)
{
    pfservices::ThumbnailCache cache;
    EXPECT_EQ(cache.maxEntries(), pfservices::ThumbnailCache::kDefaultMaxEntries);
    EXPECT_EQ(cache.maxEntries(), std::size_t{64});
}

TEST(ThumbnailCache, CapacityCanBeChanged)
{
    pfservices::ThumbnailCache cache;
    cache.setMaxEntries(128);
    EXPECT_EQ(cache.maxEntries(), std::size_t{128});
}

TEST(ThumbnailCache, RejectsZeroCapacity)
{
    EXPECT_THROW(pfservices::ThumbnailCache(0), std::invalid_argument);

    pfservices::ThumbnailCache cache;
    EXPECT_THROW(cache.setMaxEntries(0), std::invalid_argument);
    EXPECT_EQ(cache.maxEntries(), pfservices::ThumbnailCache::kDefaultMaxEntries);
}

} // namespace
