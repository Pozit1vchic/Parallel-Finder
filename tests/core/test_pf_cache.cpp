#include <gtest/gtest.h>

#include <pfservices/PfCache.hpp>

#include <filesystem>
#include <QCryptographicHash>
#include <QTemporaryDir>
#include <fstream>
#include <chrono>
#include <atomic>
#include <thread>
#include <pfcore/PathEncoding.hpp>

TEST(PfCache, SharedBudgetCountsOwnershipAndProtectsPrimaryAcrossDirectories)
{
    QTemporaryDir directory;ASSERT_TRUE(directory.isValid());
    const auto root=std::filesystem::path(directory.path().toStdWString());
    const auto refinement=root/"refinement";
    auto budget=std::make_shared<pfservices::CacheBudget>(std::vector{root,refinement},900);
    pfservices::PfCache primary(root,4096,budget),review(refinement,4096,budget);
    std::string error;const std::vector<std::uint8_t> data(230,7);
    ASSERT_TRUE(primary.resetSource("video",error));
    ASSERT_TRUE(primary.putForSource("video","motion-v43|source=video",data,error))<<error;
    const auto generation=primary.sourceGeneration("video");
    ASSERT_TRUE(review.putForSource("video","head-orientation-v1|one",data,error))<<error;
    ASSERT_TRUE(review.putForSource("video","head-orientation-v1|two",data,error))<<error;
    EXPECT_TRUE(primary.get("motion-v43|source=video"));
    EXPECT_FALSE(review.get("head-orientation-v1|one"));
    EXPECT_TRUE(review.get("head-orientation-v1|two"));
    EXPECT_LE(budget->bytesUsed(),900U);
    std::uintmax_t actual=0;
    for(const auto& dir:{root,refinement})for(const auto& entry:std::filesystem::directory_iterator(dir))
        if(entry.is_regular_file())actual+=entry.file_size();
    EXPECT_EQ(budget->bytesUsed(),actual);
    EXPECT_EQ(primary.sourceGeneration("video"),generation);
    EXPECT_THROW(pfservices::PfCache(root/"unregistered",4096,budget),std::invalid_argument);
}

TEST(PfCache, SharedBudgetCountsUnknownFilesButNeverDeletesThemOrGenerations)
{
    QTemporaryDir directory;ASSERT_TRUE(directory.isValid());
    const auto root=std::filesystem::path(directory.path().toStdWString());
    pfservices::PfCache initial(root,4096);std::string error;
    ASSERT_TRUE(initial.resetSource("video",error));
    const auto generation=initial.sourceGeneration("video");ASSERT_FALSE(generation.empty());
    const auto unknown=root/"user-note.txt";
    {std::ofstream output(unknown);output<<std::string(300,'x');}
    pfservices::CacheBudget budget({root,root/"."},200);
    EXPECT_FALSE(budget.enforceLimit(error));EXPECT_FALSE(error.empty());
    EXPECT_TRUE(std::filesystem::exists(unknown));
    EXPECT_EQ(initial.sourceGeneration("video"),generation);
    EXPECT_GT(budget.bytesUsed(),300U);
}

TEST(PfCache, SharedBudgetSerializesConcurrentWritesAcrossDirectories)
{
    QTemporaryDir directory;ASSERT_TRUE(directory.isValid());
    const auto root=std::filesystem::path(directory.path().toStdWString());
    const auto other=root/"other";
    auto budget=std::make_shared<pfservices::CacheBudget>(std::vector{root,other},800);
    pfservices::PfCache primary(root,4096,budget);std::string error;
    ASSERT_TRUE(primary.put("motion-v43|source=video",std::vector<std::uint8_t>(250,1),error));
    std::vector<std::thread> threads;
    for(int worker=0;worker<4;++worker)threads.emplace_back([&,worker]{
        pfservices::PfCache cache(worker%2?other:root,4096,budget);
        for(int i=0;i<25;++i){std::string ignored;cache.putForSource("video","preview-v1|"+std::to_string(worker)+"|"+std::to_string(i),std::vector<std::uint8_t>(130,2),ignored);}
    });
    for(auto& thread:threads)thread.join();
    EXPECT_LE(budget->bytesUsed(),800U);
    EXPECT_TRUE(primary.get("motion-v43|source=video"));
}

TEST(PfCache, SharedCapacityReplacesOldSubdirectoryCapsAndRetainsRefinementBeforePreviews)
{
    QTemporaryDir directory;ASSERT_TRUE(directory.isValid());
    const auto root=std::filesystem::path(directory.path().toStdWString());
    const auto refinement=root/"refinement";
    auto budget=std::make_shared<pfservices::CacheBudget>(std::vector{root,refinement},800);
    pfservices::PfCache primary(root,800,budget),review(refinement,64,budget);
    EXPECT_EQ(review.limitBytes(),800U);
    std::string error;const std::vector<std::uint8_t> data(270,2);
    const std::string head="head-orientation-v1|one",preview="preview-v1|one";
    ASSERT_TRUE(review.put(head,data,error))<<error;
    ASSERT_TRUE(primary.put(preview,data,error))<<error;
    const auto headPath=refinement/(QCryptographicHash::hash(QByteArray::fromStdString(head),QCryptographicHash::Sha256).toHex().toStdString()+".pfc");
    std::filesystem::last_write_time(headPath,std::filesystem::file_time_type::clock::now()-std::chrono::hours(1));
    ASSERT_TRUE(primary.put("matcher-results-v1|one",data,error))<<error;
    EXPECT_TRUE(review.get(head));EXPECT_FALSE(primary.get(preview));
    EXPECT_LE(budget->bytesUsed(),800U);
}

TEST(PfCache, UnicodePathsAndAtomicReplacementPreserveOtherSources)
{
    QTemporaryDir directory; ASSERT_TRUE(directory.isValid());
    const auto root=std::filesystem::path(directory.path().toStdWString())
        / pfcore::pathFromUtf8("Кэш 日本語 spaces 🖤");
    EXPECT_EQ(pfcore::pathFromUtf8(pfcore::pathToUtf8(root)),root);
    pfservices::PfCache cache(root,65536);
    std::string error;
    ASSERT_TRUE(cache.put("target",{1,2,3},error)) << error;
    ASSERT_TRUE(cache.put("other",{8,9},error)) << error;
    ASSERT_TRUE(cache.put("target",{4,5,6},error)) << error;
    EXPECT_EQ(*cache.get("target"),(std::vector<std::uint8_t>{4,5,6}));
    EXPECT_EQ(*cache.get("other"),(std::vector<std::uint8_t>{8,9}));
    EXPECT_FALSE(cache.put("target",std::vector<std::uint8_t>(65537),error));
    EXPECT_EQ(*cache.get("target"),(std::vector<std::uint8_t>{4,5,6}));
}

TEST(PfCache, RecentExpensiveObservationsOutliveReplaceablePreviews)
{
    QTemporaryDir directory;ASSERT_TRUE(directory.isValid());
    const auto root=std::filesystem::path(directory.path().toStdWString());
    pfservices::PfCache cache(root,512);
    const std::string observations="motion-v43|source=video",oldPreview="preview-v1|older",newPreview="preview-v1|newer";
    const auto path=[&](const std::string& key){return root/(QCryptographicHash::hash(QByteArray::fromStdString(key),QCryptographicHash::Sha256).toHex().toStdString()+".pfc");};
    std::string error;const std::vector<std::uint8_t> data(180,7);
    ASSERT_TRUE(cache.put(observations,data,error));
    ASSERT_TRUE(cache.put(oldPreview,data,error));
    const auto now=std::filesystem::file_time_type::clock::now();
    std::filesystem::last_write_time(path(observations),now-std::chrono::hours(72));
    std::filesystem::last_write_time(path(oldPreview),now-std::chrono::hours(1));
    ASSERT_TRUE(cache.put(newPreview,data,error))<<error;
    EXPECT_EQ(cache.get(observations),std::optional(data));
    EXPECT_FALSE(cache.get(oldPreview));EXPECT_TRUE(cache.get(newPreview));
    EXPECT_LE(cache.bytesUsed(),cache.limitBytes());
}

TEST(PfCache, EvictionAndEraseRemoveOwnershipWithoutLosingGeneration)
{
    QTemporaryDir directory;ASSERT_TRUE(directory.isValid());
    pfservices::PfCache cache(directory.path().toStdWString(),512);
    const std::string source="video",other="other";
    const std::string raw="motion-v43|source=video",oldPreview="preview-v1|old",newPreview="preview-v1|new";
    std::string error;
    ASSERT_TRUE(cache.resetSource(source,error));
    const auto generation=cache.sourceGeneration(source);ASSERT_FALSE(generation.empty());
    ASSERT_TRUE(cache.putForSource(source,raw,std::vector<std::uint8_t>(180,1),error));
    ASSERT_TRUE(cache.putForSource(other,oldPreview,std::vector<std::uint8_t>(180,2),error));
    const auto oldFile=std::filesystem::path(directory.path().toStdWString())/
        (QCryptographicHash::hash(QByteArray::fromStdString(oldPreview),QCryptographicHash::Sha256).toHex().toStdString()+".pfc");
    std::filesystem::last_write_time(oldFile,std::filesystem::file_time_type::clock::now()-std::chrono::hours(1));
    ASSERT_TRUE(cache.putForSource(other,newPreview,std::vector<std::uint8_t>(180,3),error));
    EXPECT_FALSE(cache.get(oldPreview));EXPECT_TRUE(cache.get(raw));
    EXPECT_EQ(cache.keysForSource(other),(std::vector<std::string>{newPreview}));
    EXPECT_EQ(cache.sourceGeneration(source),generation);
    ASSERT_TRUE(cache.erase(newPreview,error));EXPECT_TRUE(cache.keysForSource(other).empty());
    EXPECT_EQ(cache.sourceGeneration(source),generation);
}

TEST(PfCache, FailedNewOwnedWriteDoesNotAccumulateMetadata)
{
    QTemporaryDir directory;ASSERT_TRUE(directory.isValid());
    pfservices::PfCache cache(directory.path().toStdWString(),256);std::string error;
    EXPECT_FALSE(cache.putForSource("video","too-large",std::vector<std::uint8_t>(512),error));
    EXPECT_TRUE(cache.keysForSource("video").empty());
    EXPECT_FALSE(cache.putForSourceBatch("video",{{"too-large",std::vector<std::uint8_t>(512)}},error));
    EXPECT_TRUE(cache.keysForSource("video").empty());
    ASSERT_TRUE(cache.putForSource("video","existing",{1,2,3},error));
    EXPECT_FALSE(cache.putForSource("video","existing",std::vector<std::uint8_t>(512),error));
    EXPECT_EQ(cache.get("existing"),std::optional(std::vector<std::uint8_t>{1,2,3}));
    EXPECT_EQ(cache.keysForSource("video"),(std::vector<std::string>{"existing"}));
}

TEST(PfCache, CapacityPressureReclaimsLegacyOrphanOwnersOnly)
{
    QTemporaryDir directory;ASSERT_TRUE(directory.isValid());
    const auto root=std::filesystem::path(directory.path().toStdWString());
    pfservices::PfCache original(root,4096);std::string error;
    ASSERT_TRUE(original.resetSource("video",error));
    const auto generation=original.sourceGeneration("video");
    ASSERT_TRUE(original.rememberSourceKey("video","legacy-orphan",error));
    ASSERT_TRUE(original.putForSource("video","motion-v43|source=video",std::vector<std::uint8_t>(180,1),error));
    ASSERT_TRUE(original.put("preview-v1|large",std::vector<std::uint8_t>(500),error));
    pfservices::PfCache reduced(root,512);ASSERT_TRUE(reduced.enforceLimit(error))<<error;
    EXPECT_EQ(reduced.keysForSource("video"),(std::vector<std::string>{"motion-v43|source=video"}));
    EXPECT_EQ(reduced.sourceGeneration("video"),generation);
}

TEST(PfCache, ExpiredProtectionReturnsObservationsToOrdinaryLru)
{
    QTemporaryDir directory;ASSERT_TRUE(directory.isValid());
    const auto root=std::filesystem::path(directory.path().toStdWString());
    pfservices::PfCache cache(root,512);std::string error;
    const std::string observations="motion-v43|source=video";
    ASSERT_TRUE(cache.put(observations,std::vector<std::uint8_t>(180,1),error));
    const auto name=QCryptographicHash::hash(QByteArray::fromStdString(observations),QCryptographicHash::Sha256).toHex().toStdString()+".pfc";
    std::filesystem::last_write_time(root/name,std::filesystem::file_time_type::clock::now()-std::chrono::hours(24*8));
    ASSERT_TRUE(cache.put("preview-v1|older",std::vector<std::uint8_t>(180,2),error));
    ASSERT_TRUE(cache.put("preview-v1|newer",std::vector<std::uint8_t>(180,3),error));
    EXPECT_FALSE(cache.get(observations));
    EXPECT_TRUE(cache.get("preview-v1|newer"));EXPECT_LE(cache.bytesUsed(),cache.limitBytes());
}

TEST(PfCache, DeclinesCheapPreviewRatherThanLosingRecentAnalysis)
{
    QTemporaryDir directory;ASSERT_TRUE(directory.isValid());
    pfservices::PfCache cache(std::filesystem::path(directory.path().toStdWString()),256);
    std::string error;const std::vector<std::uint8_t> observations(180,1);
    ASSERT_TRUE(cache.put("motion-v43|source=video",observations,error));
    EXPECT_FALSE(cache.put("preview-v1|large",std::vector<std::uint8_t>(64,2),error));
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(cache.get("motion-v43|source=video"),std::optional(observations));
    EXPECT_FALSE(cache.get("preview-v1|large"));EXPECT_LE(cache.bytesUsed(),cache.limitBytes());
}

TEST(PfCache, RecentMatcherResultsOutlivePreviewsButYieldToObservations)
{
    QTemporaryDir directory;ASSERT_TRUE(directory.isValid());
    const auto root=std::filesystem::path(directory.path().toStdWString());
    pfservices::PfCache cache(root,512);std::string error;
    ASSERT_TRUE(cache.put("matcher-results-v1|first",std::vector<std::uint8_t>(180,1),error));
    ASSERT_TRUE(cache.put("preview-v1|first",std::vector<std::uint8_t>(180,2),error));
    const auto name=QCryptographicHash::hash("matcher-results-v1|first",QCryptographicHash::Sha256).toHex().toStdString()+".pfc";
    std::filesystem::last_write_time(root/name,std::filesystem::file_time_type::clock::now()-std::chrono::hours(72));
    const auto previewName=QCryptographicHash::hash("preview-v1|first",QCryptographicHash::Sha256).toHex().toStdString()+".pfc";
    std::filesystem::last_write_time(root/previewName,std::filesystem::file_time_type::clock::now()-std::chrono::hours(1));
    ASSERT_TRUE(cache.put("preview-v1|second",std::vector<std::uint8_t>(180,3),error));
    EXPECT_TRUE(cache.get("matcher-results-v1|first"));EXPECT_FALSE(cache.get("preview-v1|first"));
    ASSERT_TRUE(cache.put("motion-v43|source=a",std::vector<std::uint8_t>(180,4),error));
    ASSERT_TRUE(cache.put("motion-v43|source=b",std::vector<std::uint8_t>(180,5),error));
    EXPECT_FALSE(cache.get("matcher-results-v1|first"));
    EXPECT_TRUE(cache.get("motion-v43|source=a"));EXPECT_TRUE(cache.get("motion-v43|source=b"));
}

TEST(PfCache, EntryHeaderFitsBudgetBeforeReplacingAnExistingValue)
{
    QTemporaryDir directory;ASSERT_TRUE(directory.isValid());
    pfservices::PfCache cache(std::filesystem::path(directory.path().toStdWString()),64);
    std::string error;ASSERT_TRUE(cache.put("target",{1},error));
    EXPECT_FALSE(cache.put("target",std::vector<std::uint8_t>(40,2),error));
    EXPECT_FALSE(error.empty());EXPECT_EQ(cache.get("target"),std::optional(std::vector<std::uint8_t>{1}));
    EXPECT_LE(cache.bytesUsed(),cache.limitBytes());
}

TEST(PfCache, ReducedCapacityIsAppliedWithoutWritingNewData)
{
    QTemporaryDir directory;ASSERT_TRUE(directory.isValid());
    const auto root=std::filesystem::path(directory.path().toStdWString());
    pfservices::PfCache original(root,2048);std::string error;
    ASSERT_TRUE(original.put("motion-v43|source=video",std::vector<std::uint8_t>(180,1),error));
    for(int i=0;i<3;++i)ASSERT_TRUE(original.put("preview-v1|"+std::to_string(i),std::vector<std::uint8_t>(180,2),error));
    ASSERT_GT(original.bytesUsed(),512U);
    pfservices::PfCache reduced(root,512);
    ASSERT_TRUE(reduced.enforceLimit(error))<<error;
    EXPECT_LE(reduced.bytesUsed(),512U);
    EXPECT_TRUE(reduced.get("motion-v43|source=video"));
}

TEST(PfCache, ConcurrentPressureOnTheSameRootPreservesRecentObservations)
{
    QTemporaryDir directory;ASSERT_TRUE(directory.isValid());
    const auto root=std::filesystem::path(directory.path().toStdWString());
    pfservices::PfCache cache(root,512);std::string error;
    const std::string key="motion-v43|source=video";const std::vector<std::uint8_t> data(180,1);
    ASSERT_TRUE(cache.put(key,data,error));
    std::vector<std::jthread> writers;
    for(int worker=0;worker<4;++worker)writers.emplace_back([&,worker] {
        pfservices::PfCache local(worker%2 ? root/".":root,512);
        for(int i=0;i<32;++i) {
            std::string localError;
            local.put("preview-v1|"+std::to_string(worker)+"|"+std::to_string(i),
                std::vector<std::uint8_t>(180,2),localError);
        }
    });
    writers.clear();
    EXPECT_EQ(cache.get(key),std::optional(data));
    EXPECT_LE(cache.bytesUsed(),cache.limitBytes());
}

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
#ifdef _WIN32
    // A reset through another spelling of the same Windows path must rotate
    // the namespace used by the normal spelling too, even if an in-flight
    // analysis subsequently writes an entry from the previous generation.
    EXPECT_EQ(cache.sourceGeneration(source), generation);
#endif
    ASSERT_TRUE(cache.putForSource(source,"new-observations",payload,error));
    ASSERT_TRUE(cache.resetSource(source,error));
    EXPECT_FALSE(cache.get("new-observations"));
    EXPECT_NE(cache.sourceGeneration(source),generation);
    EXPECT_TRUE(cache.get("other-opaque"));
}

TEST(PfCache, BatchSourceWritesRemainReadableAndResetByOwnership)
{
    QTemporaryDir directory;ASSERT_TRUE(directory.isValid());
    pfservices::PfCache cache(directory.path().toStdString(),65536);
    const std::string source="D:/videos/batch.mp4";
    const std::vector<std::pair<std::string,std::vector<std::uint8_t>>> entries{
        {"head-sample-1",{1,2,3}},{"head-sample-2",{4,5,6}},{"head-sample-3",{7,8,9}}};
    std::string error;
    ASSERT_TRUE(cache.putForSourceBatch(source,entries,error))<<error;
    ASSERT_EQ(cache.keysForSource(source).size(),entries.size());
    for(const auto& [key,payload]:entries) {
        const auto actual=cache.get(key);ASSERT_TRUE(actual);EXPECT_EQ(*actual,payload);
    }
    ASSERT_TRUE(cache.putForSource("D:/videos/other.mp4","other-sample",{10,11},error))<<error;
    ASSERT_TRUE(cache.resetSource(source,error))<<error;
    for(const auto& [key,payload]:entries) { (void)payload;EXPECT_FALSE(cache.get(key)); }
    EXPECT_TRUE(cache.get("other-sample"));
}

TEST(PfCache, ResetFindsLongOwnedKeysAndLongSourceNames)
{
    QTemporaryDir directory; ASSERT_TRUE(directory.isValid());
    pfservices::PfCache cache(std::filesystem::path(directory.path().toStdWString()), 131072);
    const std::string source = "D:/" + std::string(4500, 's') + ".mp4";
    const std::string owned = "opaque-" + std::string(3000, 'k');
    const std::string other = "other-" + std::string(3000, 'k');
    std::string error;
    ASSERT_TRUE(cache.putForSource(source, owned, {1,2,3}, error)) << error;
    ASSERT_TRUE(cache.putForSource("D:/other.mp4", other, {4,5,6}, error)) << error;
    const auto keys = cache.keysForSource(source, &error);
    ASSERT_TRUE(error.empty()) << error;
    ASSERT_EQ(keys.size(), 1U);
    EXPECT_EQ(keys.front(), owned);
    ASSERT_TRUE(cache.resetSource(source, error)) << error;
    EXPECT_FALSE(cache.get(owned));
    EXPECT_TRUE(cache.get(other));
    EXPECT_FALSE(cache.sourceGeneration(source).empty());
}

TEST(PfCache, BatchWritesPreserveEntriesAndEnforceLimitAfterPartialFailure)
{
    QTemporaryDir directory; ASSERT_TRUE(directory.isValid());
    pfservices::PfCache cache(std::filesystem::path(directory.path().toStdWString()), 400);
    const std::vector<std::pair<std::string,std::vector<std::uint8_t>>> first {
        {"one",std::vector<std::uint8_t>(70,1)}, {"two",std::vector<std::uint8_t>(70,2)}};
    std::string error;
    ASSERT_TRUE(cache.putBatch(first,error)) << error;
    for (const auto& [key,value] : first) { const auto read=cache.get(key); ASSERT_TRUE(read); EXPECT_EQ(*read,value); }
    const std::vector<std::pair<std::string,std::vector<std::uint8_t>>> partial {
        {"three",std::vector<std::uint8_t>(70,3)}, {"too-large",std::vector<std::uint8_t>(401,9)},
        {"four",std::vector<std::uint8_t>(70,4)}};
    EXPECT_FALSE(cache.putBatch(partial,error));
    EXPECT_FALSE(error.empty());
    EXPECT_LE(cache.bytesUsed(),cache.limitBytes());
    ASSERT_TRUE(cache.get("four"));
    EXPECT_EQ(cache.get("four")->front(),4);
    EXPECT_FALSE(cache.get("too-large"));
}

TEST(PfCache, SourceBatchEnforcesLimitAfterPartialFailure)
{
    QTemporaryDir directory; ASSERT_TRUE(directory.isValid());
    pfservices::PfCache cache(std::filesystem::path(directory.path().toStdWString()), 300);
    std::string error;
    ASSERT_TRUE(cache.put("older", std::vector<std::uint8_t>(135,1), error)) << error;
    const std::vector<std::pair<std::string,std::vector<std::uint8_t>>> entries {
        {"newer", std::vector<std::uint8_t>(135,2)},
        {std::string(16385,'x'), {3}}};
    EXPECT_FALSE(cache.putForSourceBatch("D:/film.mp4", entries, error));
    EXPECT_FALSE(error.empty());
    EXPECT_LE(cache.bytesUsed(), cache.limitBytes());
    EXPECT_TRUE(cache.get("newer"));
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

TEST(PfCache, LongObservationKeysRoundTripAndRemainBounded)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    pfservices::PfCache cache(directory.path().toStdString(), 1024U * 1024U);
    std::string error;
    const std::string key = "motion-v43|" + std::string(2048, 'a');
    const std::vector<std::uint8_t> payload{1, 2, 3};
    ASSERT_TRUE(cache.putForSource("D:/video.mp4", key, payload, error)) << error;
    ASSERT_TRUE(cache.get(key));
    EXPECT_EQ(*cache.get(key), payload);
    EXPECT_FALSE(cache.put(std::string(16U * 1024U + 1, 'a'), payload, error));
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
