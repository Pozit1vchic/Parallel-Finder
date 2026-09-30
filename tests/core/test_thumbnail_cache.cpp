#include <gtest/gtest.h>

#include <pfservices/ThumbnailCache.hpp>
#include <pfservices/PreviewImage.hpp>
#include <QTemporaryDir>

#include <stdexcept>

namespace {

TEST(ThumbnailCache, FasterPreviewCompressionPreservesEveryPixel)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    QImage original(128, 72, QImage::Format_RGBA8888);
    for (int y = 0; y < original.height(); ++y)
        for (int x = 0; x < original.width(); ++x)
            original.setPixel(x, y, qRgba(x * 7 % 256, y * 11 % 256, (x + y) * 13 % 256,
                                         (x + 3 * y) % 256));
    const auto copy = original.copy();
    const auto path = directory.filePath("preview.png");
    ASSERT_TRUE(pfservices::saveLosslessPreview(original, path));
    const QImage restored(path);
    ASSERT_FALSE(restored.isNull());
    EXPECT_TRUE(restored.convertToFormat(QImage::Format_RGBA8888) == original);
    EXPECT_TRUE(original == copy);
    EXPECT_FALSE(pfservices::saveLosslessPreview(original, directory.filePath("missing/preview.png")));
}

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
