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

TEST(ThumbnailCache, EvictsLeastRecentlyUsedEntry)
{
    pfservices::ThumbnailCache cache(2);
    cache.put("a", {1});
    cache.put("b", {2});
    ASSERT_TRUE(cache.get("a").has_value()); // touch a; b is now oldest
    cache.put("c", {3});
    EXPECT_TRUE(cache.get("a").has_value());
    EXPECT_FALSE(cache.get("b").has_value());
    ASSERT_TRUE(cache.get("c").has_value());
    EXPECT_EQ(cache.get("c")->front(), 3);
}

TEST(ThumbnailCache, ShrinkingCapacityEvictsImmediately)
{
    pfservices::ThumbnailCache cache(3);
    cache.put("a", {1});
    cache.put("b", {2});
    cache.put("c", {3});
    cache.setMaxEntries(1);
    EXPECT_EQ(cache.size(), 1U);
    EXPECT_TRUE(cache.get("c").has_value());
}

} // namespace
