#include <pfservices/CharacterAnalysisCache.hpp>
#include <gtest/gtest.h>
#include <fstream>
#include <chrono>
#include <iostream>
#include <cstdlib>
#include <cstring>

namespace {
pfservices::CharacterAnalysis artifact()
{
    pfservices::CharacterAnalysis a;
    pfcore::CharacterCandidate c;
    c.id=3; c.trackIds={3}; c.observationCount=5;
    c.presenceSeconds=1; c.evidenceQuality=.8;
    c.prototype.face={1,0}; c.prototype.faceEvidence=1;
    c.distinctIdentity=true;
    c.appearances={{3,.25,.8},{3,.75,.8}};
    a.candidates.push_back(c);
    pfcore::MotionWindow w;
    w.sourceId="source"; w.trackId=3;
    w.frames={pfcore::PoseFrame{0,{{1,2,1}}},pfcore::PoseFrame{1,{{2,3,1}}}};
    a.windows.push_back(w); a.sceneCount=2;
    return a;
}
}

TEST(CharacterAnalysisCache, RetainsAllCandidatesWindowsAndInspectionEvidence)
{
    const auto a=artifact(); pfservices::CharacterAnalysis loaded;
    ASSERT_TRUE(pfservices::deserializeCharacterAnalysis(pfservices::serializeCharacterAnalysis(a),loaded));
    ASSERT_EQ(loaded.candidates.size(),1); ASSERT_EQ(loaded.windows.size(),1);
    EXPECT_EQ(loaded.sceneCount,2); EXPECT_EQ(loaded.candidates[0].id,3);
    EXPECT_EQ(loaded.candidates[0].appearances.size(),2);
    EXPECT_EQ(loaded.candidates[0].prototype.face,a.candidates[0].prototype.face);
    EXPECT_TRUE(loaded.candidates[0].distinctIdentity);
    EXPECT_EQ(loaded.windows[0].frames.size(),2);
}

TEST(CharacterAnalysisCache, TruncationNeverPublishesPartialArtifact)
{
    const auto bytes=pfservices::serializeCharacterAnalysis(artifact());
    for (std::size_t n=0;n<bytes.size();++n) {
        auto output=artifact(); output.sceneCount=9;
        EXPECT_FALSE(pfservices::deserializeCharacterAnalysis(std::span(bytes).first(n),output)) << n;
        EXPECT_EQ(output.sceneCount,9);
    }
}

TEST(CharacterAnalysisCache, RejectsForeignWindowsDuplicateTracksAndIncompatibleVersion)
{
    auto a=artifact(); pfservices::CharacterAnalysis output;
    a.windows[0].trackId=7;
    EXPECT_FALSE(pfservices::deserializeCharacterAnalysis(pfservices::serializeCharacterAnalysis(a),output));
    a=artifact(); a.candidates.push_back(a.candidates.front()); a.candidates.back().id=7;
    EXPECT_FALSE(pfservices::deserializeCharacterAnalysis(pfservices::serializeCharacterAnalysis(a),output));
    auto bytes=pfservices::serializeCharacterAnalysis(artifact()); bytes[4]=99;
    EXPECT_FALSE(pfservices::deserializeCharacterAnalysis(bytes,output));
}

TEST(CharacterAnalysisCache, DetectsSameSizeCorruptionAndCooperativeWriteCancellation)
{
    auto bytes=pfservices::serializeCharacterAnalysis(artifact());
    bytes[bytes.size()/2]^=1;
    pfservices::CharacterAnalysis output;
    EXPECT_FALSE(pfservices::deserializeCharacterAnalysis(bytes,output));
    EXPECT_TRUE(pfservices::serializeCharacterAnalysis(artifact(),[]{return true;}).empty());
    int calls=0;
    EXPECT_TRUE(pfservices::serializeCharacterAnalysis(artifact(),[&]{return ++calls>2;}).empty());
}

TEST(CharacterAnalysisCache, RealCacheParallelEncodingMatchesExistingBytes) {
    const auto* path=std::getenv("PF_TEST_CHARACTER_CACHE_FILE");
    if(!path) GTEST_SKIP() << "Set PF_TEST_CHARACTER_CACHE_FILE for real cache comparison";
    std::ifstream input(path,std::ios::binary); ASSERT_TRUE(input);
    std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(input)),{});
    ASSERT_GT(data.size(),24u); std::uint32_t keySize=0; std::memcpy(&keySize,data.data()+12,4);
    ASSERT_LT(24u+keySize,data.size()); std::span payload(data.data()+24+keySize,data.size()-24-keySize);
    pfservices::CharacterAnalysis analysis; ASSERT_TRUE(pfservices::deserializeCharacterAnalysis(payload,analysis));
    const auto running=[] {return false;};
    const auto start=std::chrono::steady_clock::now();
    const auto serial=pfservices::serializeCharacterAnalysis(analysis.candidates,analysis.windows,analysis.sceneCount,running,1);
    const auto mid=std::chrono::steady_clock::now();
    const auto parallel=pfservices::serializeCharacterAnalysis(analysis.candidates,analysis.windows,analysis.sceneCount,running,4);
    const auto end=std::chrono::steady_clock::now();
    std::cout << "REAL_CACHE windows=" << analysis.windows.size() << " bytes=" << payload.size()
              << " serial_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(mid-start).count()
              << " parallel_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(end-mid).count() << "\n";
    EXPECT_TRUE(std::equal(serial.begin(),serial.end(),payload.begin(),payload.end()));
    EXPECT_EQ(parallel,serial);
}

TEST(CharacterAnalysisCache, ReadsLargeCandidateHeaderWrittenByTheApplication) {
    pfservices::CharacterAnalysis source;
    for(std::size_t i=0;i<1100;++i) {
        pfcore::CharacterCandidate c; c.id=i+1; c.trackIds={i+1}; c.observationCount=1;
        c.prototype.body.assign(4096,.25f); source.candidates.push_back(std::move(c));
    }
    const auto bytes=pfservices::serializeCharacterAnalysis(source);
    ASSERT_GT(bytes.size(),16U*1024U*1024U);
    pfservices::CharacterAnalysis loaded;
    ASSERT_TRUE(pfservices::deserializeCharacterAnalysis(bytes,loaded));
    EXPECT_EQ(loaded.candidates.size(),source.candidates.size());
}

TEST(CharacterAnalysisCache, OptInIncrementalMatcherBenchmark) {
    const auto* path=std::getenv("PF_TEST_CHARACTER_CACHE_FILE");
    if(!path) GTEST_SKIP() << "Set PF_TEST_CHARACTER_CACHE_FILE for matcher benchmark";
    std::ifstream input(path,std::ios::binary); ASSERT_TRUE(input);
    std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(input)),{});
    ASSERT_GT(data.size(),24u); std::uint32_t keySize=0; std::memcpy(&keySize,data.data()+12,4);
    ASSERT_LT(24u+keySize,data.size()); std::span payload(data.data()+24+keySize,data.size()-24-keySize);
    pfservices::CharacterAnalysis analysis; ASSERT_TRUE(pfservices::deserializeCharacterAnalysis(payload,analysis));
    const std::size_t sampleCount=std::getenv("PF_TEST_MATCHER_WINDOW_COUNT")
        ? std::strtoull(std::getenv("PF_TEST_MATCHER_WINDOW_COUNT"),nullptr,10) : 1600;
    ASSERT_GE(analysis.windows.size(),sampleCount); ASSERT_GE(sampleCount,4u);
    analysis.windows.resize(sampleCount);
    pfcore::MotionMatcherParams params; params.allowStaticFrames=true;params.requireAppearance=false;
    params.individualPairs=true;params.recoverUnusedShots=false;params.maxUniqueResults=100;
    params.maxComparisonThreads=4;
    const auto start=std::chrono::steady_clock::now();
    const auto expected=pfcore::MotionMatcher(params).findAllPairs(analysis.windows);
    const auto coldEnd=std::chrono::steady_clock::now();
    auto prefix=std::vector<pfcore::MotionWindow>(analysis.windows.begin(),analysis.windows.begin()+sampleCount*3/4);
    pfcore::MotionSearchReuse reuse(64*1024*1024);pfcore::MotionSearchControl control;control.reuse=&reuse;
    const auto prestart=std::chrono::steady_clock::now();
    (void)pfcore::MotionMatcher(params).findAllPairs(prefix,control);
    const auto preEnd=std::chrono::steady_clock::now();
    const auto actual=pfcore::MotionMatcher(params).findAllPairs(analysis.windows,control);
    const auto end=std::chrono::steady_clock::now();
    std::cout << "INCREMENTAL_MATCHER cold_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(coldEnd-start).count()
        << " prewarm_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(preEnd-prestart).count()
        << " final_ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(end-preEnd).count()
        << " rejected_hits=" << reuse.rejectedComparisonHits() << " pairs=" << actual.size() << "\n";
    ASSERT_EQ(expected.size(),actual.size());
    for(std::size_t i=0;i<actual.size();++i) {
        EXPECT_EQ(expected[i].leftIndex,actual[i].leftIndex);EXPECT_EQ(expected[i].rightIndex,actual[i].rightIndex);
        EXPECT_DOUBLE_EQ(expected[i].similarity,actual[i].similarity);
        EXPECT_DOUBLE_EQ(expected[i].leftStartSeconds,actual[i].leftStartSeconds);
        EXPECT_DOUBLE_EQ(expected[i].rightStartSeconds,actual[i].rightStartSeconds);
    }
}
