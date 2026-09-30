#include <gtest/gtest.h>

#include <pfservices/ThumbnailCache.hpp>
#include <pfservices/PreviewImage.hpp>
#include <pfservices/CachedPreview.hpp>
#include <QTemporaryDir>

#include <stdexcept>

namespace {

TEST(CachedPreview, ReusesPixelsAcrossTemporarySessionsAndRecoversCorruptEntries)
{
    QTemporaryDir directory, sessionA, sessionB;
    ASSERT_TRUE(directory.isValid() && sessionA.isValid() && sessionB.isValid());
    pfservices::PfCache cache(std::filesystem::path(directory.path().toStdWString()));
    QImage image(32, 16, QImage::Format_RGBA8888); image.fill(Qt::blue);
    int calls = 0; bool hit = false;
    auto render = [&] {
        ++calls;
        const auto path = sessionA.filePath("render.png");
        return image.save(path) ? QUrl::fromLocalFile(path).toString() : QString{};
    };
    const auto first = pfservices::cachedPreview(&cache, "source-v1|frame-123", sessionA.path(), render, hit);
    ASSERT_FALSE(first.isEmpty()); EXPECT_FALSE(hit); EXPECT_EQ(calls, 1);
    QFile::remove(QUrl(first).toLocalFile());
    const auto second = pfservices::cachedPreview(&cache, "source-v1|frame-123", sessionB.path(), render, hit);
    EXPECT_TRUE(hit); EXPECT_EQ(calls, 1);
    EXPECT_TRUE(QImage(QUrl(second).toLocalFile()).convertToFormat(QImage::Format_RGBA8888) == image);
    std::string error;
    ASSERT_TRUE(cache.put("broken", {1,2,3}, error));
    EXPECT_FALSE(pfservices::cachedPreview(&cache, "broken", sessionB.path(), render, hit).isEmpty());
    EXPECT_FALSE(hit); EXPECT_EQ(calls, 2);
    pfservices::cachedPreview(&cache, "different-source-fingerprint", sessionB.path(), render, hit);
    EXPECT_FALSE(hit); EXPECT_EQ(calls, 3);
}

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
