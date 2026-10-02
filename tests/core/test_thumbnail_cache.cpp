#include <gtest/gtest.h>

#include <pfservices/ThumbnailCache.hpp>
#include <pfservices/PreviewImage.hpp>
#include <pfservices/CachedPreview.hpp>
#include <pfservices/PreviewWriter.hpp>
#include <QTemporaryDir>
#include <QDir>

#include <stdexcept>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

TEST(CachedPreview, ParallelMaterializationPreservesPixelsOrderAndDuplicateKeys)
{
    QTemporaryDir directory, source, target;
    pfservices::PfCache cache(std::filesystem::path(directory.path().toStdWString()));
    std::vector<std::string> keys;
    std::vector<QImage> images;
    for (int i = 0; i < 17; ++i) {
        QImage image(127, 73, QImage::Format_RGBA8888); image.fill(qRgba(i*11, 231-i, i*13, i*7));
        const auto key = "preview-" + std::to_string(i);
        bool hit;
        ASSERT_FALSE(pfservices::cachedPreview(&cache, key, source.path(), [&] {
            const auto path = source.filePath(QString::number(i)+".png");
            return pfservices::saveLosslessPreview(image,path) ? QUrl::fromLocalFile(path).toString() : QString{};
        }, hit).isEmpty());
        keys.push_back(key); images.push_back(image);
    }
    keys.push_back(keys.front()); keys.push_back("missing"); keys.push_back("broken");
    QFile png(source.filePath("0.png")); ASSERT_TRUE(png.open(QIODevice::ReadOnly));
    const auto truncated = png.readAll().first(50);
    std::string error;
    ASSERT_TRUE(cache.put("broken",std::vector<std::uint8_t>(truncated.begin(),truncated.end()),error));
    const auto urls = pfservices::materializeCachedPreviews(&cache,keys,target.path());
    ASSERT_EQ(urls.size(),keys.size());
    for (std::size_t i = 0; i < images.size(); ++i)
        EXPECT_TRUE(QImage(QUrl(urls[i]).toLocalFile()).convertToFormat(QImage::Format_RGBA8888) == images[i]);
    EXPECT_EQ(urls[17],urls[0]); EXPECT_TRUE(urls[18].isEmpty()); EXPECT_TRUE(urls[19].isEmpty());
    const auto cancelled = pfservices::materializeCachedPreviews(&cache,keys,target.path(),[]{return true;});
    for (const auto& url : cancelled) EXPECT_TRUE(url.isEmpty());
    EXPECT_EQ(pfservices::materializeCachedPreviews(nullptr,keys,target.path()).size(),keys.size());
}

TEST(PreviewWriter, BoundedQueueDrainsAndPreservesEncodedBytesAndCache)
{
    QTemporaryDir directory, serial, parallel, cacheSession;
    pfservices::PfCache cache(std::filesystem::path(directory.path().toStdWString()));
    std::vector<std::shared_future<QString>> futures;
    {
        pfservices::PreviewWriter writer(&cache,1);
        for (int i = 0; i < 19; ++i) {
            QImage image(128,72,QImage::Format_RGBA8888); image.fill(qRgba(i*11,231-i,i*13,i*7));
            const auto name = QString::number(i)+".png";
            ASSERT_TRUE(pfservices::saveLosslessPreview(image,serial.filePath(name)));
            futures.push_back(writer.submit(image.copy(),parallel.filePath(name),"async-"+std::to_string(i)));
            image.fill(Qt::black); // source changes after enqueue cannot change the saved still
        }
        writer.finish(); writer.finish();
        EXPECT_THROW(writer.submit({},parallel.filePath("late.png"),"late"),std::logic_error);
    }
    for (int i = 0; i < 19; ++i) {
        ASSERT_EQ(futures[i].wait_for(std::chrono::milliseconds(0)),std::future_status::ready);
        ASSERT_FALSE(futures[i].get().isEmpty());
        QFile expected(serial.filePath(QString::number(i)+".png")),actual(QUrl(futures[i].get()).toLocalFile());
        ASSERT_TRUE(expected.open(QIODevice::ReadOnly)); ASSERT_TRUE(actual.open(QIODevice::ReadOnly));
        EXPECT_EQ(actual.readAll(),expected.readAll());
        EXPECT_FALSE(pfservices::materializeCachedPreview(&cache,"async-"+std::to_string(i),cacheSession.path()).isEmpty());
    }
}
TEST(PreviewWriter, FailedWritesCompleteWithoutDeadlock)
{
    QTemporaryDir directory;
    EXPECT_THROW(pfservices::PreviewWriter(nullptr,0),std::invalid_argument);
    pfservices::PreviewWriter writer(nullptr);
    auto empty = writer.submit({},directory.filePath("empty.png"),"empty");
    QImage image(32,32,QImage::Format_RGBA8888); image.fill(Qt::red);
    auto missing = writer.submit(image,directory.filePath("missing/no.png"),"missing");
    writer.finish();
    EXPECT_TRUE(empty.get().isEmpty()); EXPECT_TRUE(missing.get().isEmpty());
}
TEST(CachedPreview, OptionalExistingCachePreparationBenchmark)
{
    const auto* root = std::getenv("PF_BENCH_PREVIEW_CACHE");
    if (!root || !*root) GTEST_SKIP() << "Opt-in diagnostic on an isolated benchmark cache";
    pfservices::PfCache cache(std::filesystem::path(std::u8string(root,root+std::strlen(root))));
    std::vector<std::string> keys;
    const QDir directory(QString::fromUtf8(root));
    for (const auto& name : directory.entryList({"*.pfc"},QDir::Files)) {
        QFile file(directory.filePath(name)); if (!file.open(QIODevice::ReadOnly)) continue;
        const auto header = file.read(24); if (header.size()!=24 || header.first(8)!="PFCACHE1") continue;
        std::uint32_t size=0;std::memcpy(&size,header.constData()+12,sizeof(size));
        if (!size || size>1024) continue;
        const auto key = file.read(size).toStdString();
        if (key.starts_with("preview-v1|")) keys.push_back(key);
    }
    ASSERT_GE(keys.size(),8U);
    for (int pass=0;pass<3;++pass) for (const std::size_t workers:{1U,4U}) {
        QTemporaryDir target;
        const auto begin = std::chrono::steady_clock::now();
        const auto urls = pfservices::materializeCachedPreviews(&cache,keys,target.path(),{},workers);
        for (const auto& url : urls) ASSERT_FALSE(url.isEmpty());
        std::fprintf(stderr,"PF_BENCH_PREVIEW_CACHE pass=%d workers=%zu images=%zu ms=%.3f\n",pass,workers,keys.size(),
            std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count());
    }
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
