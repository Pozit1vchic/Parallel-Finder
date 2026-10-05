#include <gtest/gtest.h>
#include <pfservices/MatchCache.hpp>
#include <QTemporaryDir>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>

namespace {
std::vector<pfcore::MotionWindow> windows()
{
    std::vector<pfcore::MotionWindow> windows(2);
    windows[0].sourceId = "first.mp4";
    windows[1].sourceId = "second.mp4";
    for (auto& w : windows) {
        w.frames.push_back({0.125,{{0.1,0.2,0.9}}});
        w.frames.push_back({200.0,{{0.1,0.2,0.9}}});
        w.faceEmbedding = {0.5F,0.25F};
        w.sceneContext = {0.125F};
    }
    return windows;
}
}

TEST(MatchCache, InvalidatesExactInputsSettingsAndSourceOrder)
{
    const auto original = windows();
    pfcore::MotionMatcherParams params;
    const auto key = pfservices::MatchCache::key(original,params);
    auto changed = original;
    changed[0].frames[0].keypoints[0].x = std::nextafter(0.1,1.0);
    EXPECT_NE(key,pfservices::MatchCache::key(changed,params));
    changed = original;
    changed[0].faceEmbedding[0] = std::nextafter(0.5F,1.0F);
    EXPECT_NE(key,pfservices::MatchCache::key(changed,params));
    changed = original;
    std::swap(changed[0],changed[1]);
    EXPECT_NE(key,pfservices::MatchCache::key(changed,params));
    params.similarityThreshold = std::nextafter(params.similarityThreshold,1.0);
    EXPECT_NE(key,pfservices::MatchCache::key(original,params));
    params = {};
    params.mirrorInvariant = true;
    EXPECT_NE(key,pfservices::MatchCache::key(original,params));
    params = {};
    params.maxResultsPerShot = 0;
    EXPECT_NE(key,pfservices::MatchCache::key(original,params));
    params={};params.individualPairs=true;
    EXPECT_NE(key,pfservices::MatchCache::key(original,params));
    params={};params.recoverUnusedShots=true;
    EXPECT_NE(key,pfservices::MatchCache::key(original,params));
    changed=original;changed[0].sceneView={.1F,.2F};
    EXPECT_NE(key,pfservices::MatchCache::key(changed,{}));
    changed=original;changed[0].sceneSequence={.1F,.2F};
    EXPECT_NE(key,pfservices::MatchCache::key(changed,{}));
}

TEST(MatchCache, PreservesExactScoresEmptyHitsAndRejectsCorruption)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    pfservices::PfCache cache(std::filesystem::path(directory.path().toStdWString()));
    const auto inputs = windows();
    const auto key = pfservices::MatchCache::key(inputs,{});
    EXPECT_FALSE(pfservices::MatchCache::load(cache,key,inputs));
    pfcore::MotionMatch match;
    match.leftIndex = 0; match.rightIndex = 1;
    match.leftSourceId = inputs[0].sourceId; match.rightSourceId = inputs[1].sourceId;
    match.similarity = std::nextafter(0.9,1.0);
    match.rankScore = std::nextafter(0.8,0.0);
    match.durationSeconds = 1.0/3;
    match.leftStartSeconds = 123.45678901234567;
    match.leftEndSeconds = 124.45678901234567;
    match.rightStartSeconds = 0.125;
    match.rightEndSeconds = 1.0;
    match.appearanceVerified = true;
    match.headOnlyComparison = true;
    match.directionLabel = "left \"right\"";
    const std::vector matches{match};
    std::string error;
    ASSERT_TRUE(pfservices::MatchCache::store(cache,key,matches,error)) << error;
    const auto restored = pfservices::MatchCache::load(cache,key,inputs);
    ASSERT_TRUE(restored); ASSERT_EQ(restored->size(),1U);
    EXPECT_EQ(restored->front().similarity,match.similarity);
    EXPECT_EQ(restored->front().rankScore,match.rankScore);
    EXPECT_EQ(restored->front().durationSeconds,match.durationSeconds);
    EXPECT_EQ(restored->front().leftStartSeconds,match.leftStartSeconds);
    EXPECT_EQ(restored->front().directionLabel,match.directionLabel);
    EXPECT_TRUE(restored->front().appearanceVerified);
    EXPECT_TRUE(restored->front().headOnlyComparison);
    auto payload = cache.get(key);
    ASSERT_TRUE(payload);
    EXPECT_FALSE(cache.get(key,payload->size()-1));
    (*payload)[payload->size()-2] ^= 1;
    ASSERT_TRUE(cache.put(key,*payload,error));
    EXPECT_FALSE(pfservices::MatchCache::load(cache,key,inputs));
    ASSERT_TRUE(pfservices::MatchCache::store(cache,key,{},error));
    const auto empty = pfservices::MatchCache::load(cache,key,inputs);
    ASSERT_TRUE(empty); EXPECT_TRUE(empty->empty());
}

TEST(MatchCache, RejectsInvalidMetadataEvenWithValidChecksum)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    pfservices::PfCache cache(std::filesystem::path(directory.path().toStdWString()));
    const auto inputs = windows();
    const auto key = pfservices::MatchCache::key(inputs,{});
    pfcore::MotionMatch match;
    match.leftIndex = 0; match.rightIndex = 1;
    match.leftSourceId = inputs[0].sourceId; match.rightSourceId = inputs[1].sourceId;
    match.similarity = 0.9;
    match.leftStartSeconds = match.rightStartSeconds = 1;
    match.leftEndSeconds = match.rightEndSeconds = 2;
    std::string error;
    ASSERT_TRUE(pfservices::MatchCache::store(cache,key,std::vector{match},error));
    const auto payload = cache.get(key);
    ASSERT_TRUE(payload);
    const auto original = QJsonDocument::fromJson(QByteArray(
        reinterpret_cast<const char*>(payload->data()+32),
        static_cast<qsizetype>(payload->size()-32))).object();
    const auto reject = [&](auto mutate) {
        auto document = original;
        auto records = document["matches"].toArray();
        auto record = records[0].toObject();
        mutate(record);
        records[0] = record;
        document["matches"] = records;
        const auto bytes = QJsonDocument(document).toJson(QJsonDocument::Compact);
        const auto digest = QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);
        std::vector<std::uint8_t> changed(digest.begin(),digest.end());
        changed.insert(changed.end(),bytes.begin(),bytes.end());
        ASSERT_TRUE(cache.put(key,changed,error));
        EXPECT_FALSE(pfservices::MatchCache::load(cache,key,inputs));
    };
    reject([](QJsonObject& r) { r["rightIndex"] = "18446744073709551615"; });
    reject([](QJsonObject& r) { r["leftSourceId"] = "unrelated.mp4"; });
    reject([](QJsonObject& r) { r["leftEndSeconds"] = 201; });
    reject([](QJsonObject& r) { r["rightStartSeconds"] = 3; });
    reject([](QJsonObject& r) { r["similarity"] = 1.1; });
    reject([](QJsonObject& r) { r["rankScore"] = "0.9"; });
    reject([](QJsonObject& r) { r.remove("faceVerified"); });
    reject([](QJsonObject& r) { r["appearanceVerified"] = 1; });
}
