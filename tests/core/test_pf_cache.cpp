#include <gtest/gtest.h>

#include <pfservices/PfCache.hpp>

#include <filesystem>
#include <QCryptographicHash>
#include <QTemporaryDir>
#include <fstream>

TEST(PfCache, ForgedPayloadLengthAndTrailingBytesAreCacheMisses)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    pfservices::PfCache cache(directory.path().toStdString(), 512U * 1024U * 1024U);
    const std::string key="malformed-payload";
    const auto name=QCryptographicHash::hash(QByteArray::fromStdString(key),
        QCryptographicHash::Sha256).toHex().toStdString()+".pfc";
    const auto path=std::filesystem::path(directory.path().toStdString())/name;
    std::string error;
    ASSERT_TRUE(cache.put(key,{1,2,3},error)) << error;
    {
        std::fstream file(path,std::ios::binary|std::ios::in|std::ios::out);
        ASSERT_TRUE(file);
        const std::uint64_t forged=128U*1024U*1024U;
        file.seekp(16);
        file.write(reinterpret_cast<const char*>(&forged),sizeof(forged));
    }
    EXPECT_FALSE(cache.get(key));
    ASSERT_TRUE(cache.put(key,{1,2,3},error)) << error;
    {
        std::ofstream file(path,std::ios::binary|std::ios::app);
        file.put('x');
    }
    EXPECT_FALSE(cache.get(key));
    ASSERT_TRUE(cache.put(key,{1,2,3},error)) << error;
    const auto valid=cache.get(key);
    ASSERT_TRUE(valid);
    EXPECT_EQ(*valid,(std::vector<std::uint8_t>{1,2,3}));
}

TEST(PfCache, FailedPhysicalPurgeReportsErrorAndStillInvalidatesTheOldGeneration)
{
    QTemporaryDir directory;ASSERT_TRUE(directory.isValid());
    pfservices::PfCache cache(directory.path().toStdString(),65536);
    std::string error;const std::string source="D:/selected.mp4",key="selected-observations";
    ASSERT_TRUE(cache.putForSource(source,key,{1,2,3},error));
    ASSERT_TRUE(cache.putForSource("D:/other.mp4","other-observations",{4,5},error));
    const auto filename=QCryptographicHash::hash(QByteArray::fromStdString(key),QCryptographicHash::Sha256).toHex().toStdString()+".pfc";
    const auto blocked=std::filesystem::path(directory.path().toStdString())/filename;
    ASSERT_TRUE(std::filesystem::remove(blocked));
    ASSERT_TRUE(std::filesystem::create_directory(blocked));
    {std::ofstream sentinel(blocked/"locked");sentinel<<"keep";}
    EXPECT_FALSE(cache.resetSource(source,error));
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(cache.sourceGeneration(source).empty());
    EXPECT_TRUE(cache.get("other-observations"));
    EXPECT_TRUE(std::filesystem::exists(blocked/"locked"));
}

TEST(PfCache, ResetPhysicallyDeletesOnlyTheSelectedVideoIncludingOpaqueDerivedKeys)
{
    QTemporaryDir directory;ASSERT_TRUE(directory.isValid());
    pfservices::PfCache cache(directory.path().toStdString(),65536);
    const std::string source="D:/videos/Soldier Boy.mp4",other="D:/videos/Steve.mp4";
    const std::string motion="motion-v43|quality=fast|source="+source+"|100|200";
    const std::string legacy="motion-v42|quality=precise|source=D:\\videos\\Soldier Boy.mp4|100|150";
    const std::string sequence="scene-sequence-v5|"+QCryptographicHash::hash(QByteArray::fromStdString(legacy),QCryptographicHash::Sha256).toHex().toStdString();
    const std::string face="matched-face-v7|opaque-face-hash";
    const std::string preview="preview-v1|1920x1080|"+source+"|100|200|generation=old|t=5";
    std::string error;const std::vector<std::uint8_t> payload(64,7);
    ASSERT_TRUE(cache.putForSource(other,"other-opaque",payload,error));
    const auto otherBytes=cache.bytesUsed();
    ASSERT_TRUE(cache.putForSource(source,motion,payload,error));
    ASSERT_TRUE(cache.putForSource(source,face,payload,error));
    ASSERT_TRUE(cache.put(legacy,payload,error));
    ASSERT_TRUE(cache.put(sequence,payload,error));
    ASSERT_TRUE(cache.put(preview,payload,error));
#ifdef _WIN32
    ASSERT_TRUE(cache.resetSource("d:\\VIDEOS\\soldier boy.mp4",error))<<error;
#else
    ASSERT_TRUE(cache.resetSource(source,error))<<error;
#endif
    for(const auto& key:{motion,legacy,sequence,face,preview})EXPECT_FALSE(cache.get(key));
    EXPECT_TRUE(cache.get("other-opaque"));
    EXPECT_EQ(cache.bytesUsed(),otherBytes);
    EXPECT_TRUE(cache.keysForSource(source).empty());
    const auto generation=cache.sourceGeneration(
#ifdef _WIN32
        "d:\\VIDEOS\\soldier boy.mp4"
#else
        source
#endif
    );EXPECT_FALSE(generation.empty());
    ASSERT_TRUE(cache.putForSource(source,"new-observations",payload,error));
    ASSERT_TRUE(cache.resetSource(source,error));
    EXPECT_FALSE(cache.get("new-observations"));
    EXPECT_NE(cache.sourceGeneration(source),generation);
    EXPECT_TRUE(cache.get("other-opaque"));
}

TEST(PfCache, WritesReadsAndEvictsVersionedEntries)
{
    const auto root = std::filesystem::temp_directory_path() / "parallel-finder-pfcache-test";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    pfservices::PfCache cache(root, 180);
    std::string error;
    ASSERT_TRUE(cache.put("first", std::vector<std::uint8_t>(64, 1), error)) << error;
    ASSERT_TRUE(cache.get("first").has_value());
    ASSERT_TRUE(cache.put("second", std::vector<std::uint8_t>(128, 2), error)) << error;
    EXPECT_FALSE(cache.get("first").has_value());
    ASSERT_TRUE(cache.get("second").has_value());
    EXPECT_EQ(cache.get("second")->front(), 2U);
    EXPECT_GT(cache.bytesUsed(), 0U);
    ASSERT_TRUE(cache.clear(error)) << error;
    EXPECT_EQ(cache.bytesUsed(), 0U);
    std::filesystem::remove_all(root, ignored);
}

TEST(PfCache, RejectsOversizedEntry)
{
    const auto root = std::filesystem::temp_directory_path() / "parallel-finder-pfcache-limit-test";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    pfservices::PfCache cache(root, 32);
    std::string error;
    EXPECT_FALSE(cache.put("large", std::vector<std::uint8_t>(33, 1), error));
    EXPECT_FALSE(error.empty());
    std::filesystem::remove_all(root, ignored);
}
