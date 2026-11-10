#include <pfcore/RepeatedSceneIdentity.hpp>
#include <gtest/gtest.h>

namespace {
pfcore::MotionWindow window(std::size_t track,std::size_t scene,double start,double shift=0) {
    pfcore::MotionWindow w;w.sourceId="film";w.trackId=track;w.sceneIndex=scene;w.hasSceneIndex=true;
    w.sceneStartSeconds=start;w.sceneEndSeconds=start+1;w.appearanceEmbedding={1,0};
    for(int i=0;i<3;++i) {
        w.sceneSequenceTimes.push_back(start+i*.25);
        w.sceneSequence.insert(w.sceneSequence.end(),432,.1F+i*.01F);
        w.frames.push_back({start+i*.25,{{shift,0,1},{shift+50,0,1},{shift,100,1}}});
    }
    return w;
}
std::vector<pfcore::CharacterCandidate> candidates() {
    pfcore::CharacterCandidate a,b;a.id=1;a.trackIds={1};b.id=2;b.trackIds={2};return {a,b};
}
}
TEST(RepeatedSceneIdentity, RequiresDistinctTimedFramesAndPosition) {
    auto a=window(1,0,0),b=window(2,1,10,2);
    auto result=pfcore::auditRepeatedSceneIdentityLinks(candidates(),{a,b});
    ASSERT_EQ(result.size(),1);EXPECT_EQ(result[0].leftCandidate,1);EXPECT_EQ(result[0].rightCandidate,2);
    EXPECT_TRUE(pfcore::auditRepeatedSceneIdentityLinks(candidates(),{a,window(2,1,10,300)}).empty());
    b.appearanceEmbedding={0,1};EXPECT_TRUE(pfcore::auditRepeatedSceneIdentityLinks(candidates(),{a,b}).empty());
    b=window(2,1,10);for(auto& frame:b.frames)frame.timestampSeconds=10;
    EXPECT_TRUE(pfcore::auditRepeatedSceneIdentityLinks(candidates(),{a,b}).empty());
    b=window(2,1,10);std::fill(a.sceneSequence.begin(),a.sceneSequence.end(),.1F);
    std::fill(b.sceneSequence.begin(),b.sceneSequence.end(),.1F);
    EXPECT_TRUE(pfcore::auditRepeatedSceneIdentityLinks(candidates(),{a,b}).empty());
    a=window(1,0,0);
    EXPECT_TRUE(pfcore::auditRepeatedSceneIdentityLinks(candidates(),{a,window(2,1,10)},[]{return true;}).empty());
    int polls=0;
    EXPECT_TRUE(pfcore::auditRepeatedSceneIdentityLinks(candidates(),{a,window(2,1,10)},[&]{return ++polls==1;}).empty());
}
TEST(RepeatedSceneIdentity, VetoesFaceConflictCovisibilityAndAmbiguity) {
    auto a=window(1,0,0),b=window(2,1,10);
    a.faceConfidence=b.faceConfidence=1;a.faceEmbedding={1,0};b.faceEmbedding={0,1};
    EXPECT_TRUE(pfcore::auditRepeatedSceneIdentityLinks(candidates(),{a,b}).empty());
    a.faceEmbedding.clear();b.faceEmbedding.clear();
    auto simultaneous=window(2,0,0,300);simultaneous.sceneSequence.clear();simultaneous.sceneSequenceTimes.clear();
    EXPECT_TRUE(pfcore::auditRepeatedSceneIdentityLinks(candidates(),{a,b,simultaneous}).empty());
    auto people=candidates();pfcore::CharacterCandidate c;c.id=3;c.trackIds={3};people.push_back(c);
    EXPECT_TRUE(pfcore::auditRepeatedSceneIdentityLinks(people,{a,b,window(3,2,20)}).empty());
}
TEST(RepeatedSceneIdentity, LessExactFramesNeedStrongerBodyAgreement) {
    const auto a=window(1,0,0);auto b=window(2,1,10);
    for(auto& pixel:b.sceneSequence)pixel+=.04F;
    b.appearanceEmbedding={.89F,.4559605F};
    EXPECT_TRUE(pfcore::auditRepeatedSceneIdentityLinks(candidates(),{a,b}).empty());
    b.appearanceEmbedding={.91F,.4146082F};
    EXPECT_EQ(pfcore::auditRepeatedSceneIdentityLinks(candidates(),{a,b}).size(),1);
}
TEST(RepeatedSceneIdentity, ReframedSceneNeedsThreeMovingPrecisePoses) {
    auto a=window(1,0,0),b=window(2,1,10,2);
    for(std::size_t i=0;i<a.sceneSequence.size();++i) {
        a.sceneSequence[i]=.1F+static_cast<float>(i/432)*.02F;
        b.sceneSequence[i]=a.sceneSequence[i]+.06F;
    }
    b.appearanceEmbedding={.86F,.510294F};
    EXPECT_EQ(pfcore::auditRepeatedSceneIdentityLinks(candidates(),{a,b}).size(),1);
    for(auto& point:b.frames.back().keypoints)point.x+=300;
    EXPECT_TRUE(pfcore::auditRepeatedSceneIdentityLinks(candidates(),{a,b}).empty());
    b=window(2,1,10,2);std::fill(b.sceneSequence.begin(),b.sceneSequence.end(),.16F);
    EXPECT_TRUE(pfcore::auditRepeatedSceneIdentityLinks(candidates(),{a,b}).empty());
}
TEST(RepeatedSceneIdentity, CoarseFilterPreservesPixelThresholdAndUnboundedInputs) {
    auto a=window(1,0,0),b=window(2,1,10);
    for(std::size_t i=0;i<a.sceneSequence.size();++i) {
        a.sceneSequence[i]=.2F+static_cast<float>(i/432)*.02F;
        b.sceneSequence[i]=a.sceneSequence[i]+.079F;
    }
    EXPECT_EQ(pfcore::auditRepeatedSceneIdentityLinks(candidates(),{a,b}).size(),1);
    for(std::size_t i=0;i<b.sceneSequence.size();++i)b.sceneSequence[i]=a.sceneSequence[i]+.081F;
    EXPECT_TRUE(pfcore::auditRepeatedSceneIdentityLinks(candidates(),{a,b}).empty());
    // Sums cannot distinguish swapped pixels: the exact check must still run.
    for(std::size_t i=0;i<a.sceneSequence.size();++i) {
        a.sceneSequence[i]=static_cast<float>(i%2)*.8F+static_cast<float>(i/432)*.02F;
        b.sceneSequence[i]=static_cast<float>((i+1)%2)*.8F+static_cast<float>(i/432)*.02F;
    }
    EXPECT_TRUE(pfcore::auditRepeatedSceneIdentityLinks(candidates(),{a,b}).empty());
    // Out-of-range inputs follow the old exact path, including large floats.
    for(std::size_t i=0;i<a.sceneSequence.size();++i) {
        a.sceneSequence[i]=5.F+static_cast<float>(i/432)*.02F;
        b.sceneSequence[i]=a.sceneSequence[i]+.06F;
    }
    EXPECT_EQ(pfcore::auditRepeatedSceneIdentityLinks(candidates(),{a,b}).size(),1);
}
TEST(RepeatedSceneIdentity, MergeKeepsStableIdTracksAndPreviewOrder) {
    auto people=candidates();people[0].observationCount=4;people[0].presenceSeconds=1;
    people[0].appearances.push_back({1,0,.5,{0,0,1,1}});
    people[1].observationCount=3;people[1].presenceSeconds=.75;
    people[1].appearances.push_back({2,10,.6,{0,0,1,1}});
    people[1].prototype.face={1,0};people[1].prototype.faceEvidence=1;
    EXPECT_EQ(pfcore::mergeRepeatedSceneIdentities(people,{window(1,0,0),window(2,1,10)}, {}, "other"),0);
    ASSERT_EQ(people.size(),2);
    EXPECT_EQ(pfcore::mergeRepeatedSceneIdentities(people,{window(1,0,0),window(2,1,10)}, {}, "film"),1);
    ASSERT_EQ(people.size(),1);EXPECT_EQ(people[0].id,1);EXPECT_EQ(people[0].trackIds,(std::vector<std::size_t>{1,2}));
    EXPECT_EQ(people[0].observationCount,7);EXPECT_DOUBLE_EQ(people[0].presenceSeconds,1.75);
    ASSERT_EQ(people[0].appearances.size(),2);EXPECT_EQ(people[0].appearances[0].trackId,1);
    EXPECT_EQ(people[0].appearances[1].trackId,2);EXPECT_EQ(people[0].prototype.face,(std::vector<float>{1,0}));
}

TEST(RepeatedSceneIdentity, ProgressAndEarlyBodyRejection) {
    auto a=window(1,0,0),b=window(2,1,10);
    b.appearanceEmbedding={0,1};
    std::vector<std::pair<std::size_t,std::size_t>> updates;
    const auto result=pfcore::auditRepeatedSceneIdentityLinks(candidates(),{a,b},{},"film",
        [&](auto done,auto total){updates.emplace_back(done,total);});
    EXPECT_TRUE(result.empty());
    ASSERT_GE(updates.size(),2);
    EXPECT_EQ(updates.front(),(std::pair<std::size_t,std::size_t>{0,1}));
    EXPECT_EQ(updates.back(),(std::pair<std::size_t,std::size_t>{1,1}));
    for(std::size_t i=1;i<updates.size();++i)EXPECT_GE(updates[i].first,updates[i-1].first);
}
