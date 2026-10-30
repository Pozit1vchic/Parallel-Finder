#include <pfcore/SceneFamily.hpp>
#include <gtest/gtest.h>
#include <pfcore/MotionRanker.hpp>

namespace {
pfcore::SceneOccurrence occurrence(double start,double end,double quality=.8) {
    pfcore::SceneOccurrence o;o.start=start;o.end=end;o.matchSimilarity=.9;
    o.descriptor.sourceId="film";o.descriptor.identityId=7;o.descriptor.sceneId=1;
    o.descriptor.evidenceQuality=quality;o.descriptor.temporalCoverage=.9;o.descriptor.identityConfidence=.8;
    o.descriptor.jointEvidence.fill(.9);
    return o;
}
}
TEST(SceneFamily, OverlapsCollapseAndRepresentativeUsesEvidenceRatherThanRawMatchScore) {
    auto a=occurrence(10,13,.9),b=occurrence(10.7,13.9,.3),c=occurrence(11.1,14,.5);
    a.matchSimilarity=.85;b.matchSimilarity=.99;
    const std::vector input{a,b,c};const auto families=pfcore::buildSceneFamilies(input);
    ASSERT_EQ(families.families.size(),1);
    EXPECT_EQ(families.families[0].representative,0);
    EXPECT_EQ(families.families[0].members.size(),3);
    EXPECT_FALSE(families.families[0].canonicalId.empty());
}
TEST(SceneFamily, AdjacentDuplicatesNeedPoseIdentitySceneAndContextAgreement) {
    auto a=occurrence(10,11),b=occurrence(11.05,12);
    EXPECT_EQ(pfcore::buildSceneFamilies(std::vector{a,b}).families.size(),1);
    b.descriptor.sceneId=2;
    EXPECT_EQ(pfcore::buildSceneFamilies(std::vector{a,b}).families.size(),2);
    b=occurrence(11.05,12);b.descriptor.sceneContext={0,1};a.descriptor.sceneContext={1,0};
    EXPECT_EQ(pfcore::buildSceneFamilies(std::vector{a,b}).families.size(),2);
    b=occurrence(11.05,12);a=occurrence(10,11);b.descriptor.identityId=8;
    EXPECT_EQ(pfcore::buildSceneFamilies(std::vector{a,b}).families.size(),2);
}
TEST(SceneFamily, SeparateOccurrencesAndOverlapChainsRemainSeparate) {
    EXPECT_EQ(pfcore::buildSceneFamilies(std::vector{occurrence(10,13),occurrence(50,53)}).families.size(),2);
    EXPECT_EQ(pfcore::buildSceneFamilies(std::vector{occurrence(0,2),occurrence(.8,2.8),occurrence(1.6,3.6)}).families.size(),2);
    auto missing=occurrence(10.2,13);missing.descriptor.jointEvidence.fill(0);
    EXPECT_EQ(pfcore::buildSceneFamilies(std::vector{occurrence(10,13),missing}).families.size(),2);
}
TEST(SceneFamily, ContinuityRespectsTemporalLocalityAndCancellation) {
    auto a=occurrence(2,3),b=occurrence(40,41),c=occurrence(2.2,3.2);
    c.descriptor.normalizedSkeleton.fill(.1);
    const auto order=pfcore::continuityOrder(std::vector{a,b,c});
    EXPECT_EQ(order,(std::vector<std::size_t>{0,2,1}));
    EXPECT_TRUE(pfcore::buildSceneFamilies(std::vector{a,b},[]{return true;}).cancelled);
    EXPECT_EQ(pfcore::canonicalSceneFingerprint("film",7,2,3),pfcore::canonicalSceneFingerprint("film",7,2,3));
    EXPECT_NE(pfcore::canonicalSceneFingerprint("film",7,2,3),pfcore::canonicalSceneFingerprint("film",8,2,3));
}

TEST(SceneFamily, RepeatedEditRequiresCoverageOfBothMovingPixelSequences) {
    pfcore::MotionWindow a,b;a.sourceId=b.sourceId="film";
    a.hasSceneIndex=b.hasSceneIndex=true;a.sceneIndex=1;b.sceneIndex=2;
    for(int frame=0;frame<8;++frame) {
        a.sceneSequenceTimes.push_back(frame*.25);b.sceneSequenceTimes.push_back(50+frame*.25);
        for(int pixel=0;pixel<432;++pixel) {
            const float value=.15F+float((pixel*17+frame*11)%71)/100;
            a.sceneSequence.push_back(value);b.sceneSequence.push_back(value);
        }
    }
    EXPECT_TRUE(pfcore::MotionMatcher::sameFootageRange(a,0,2,b,50,52));
    // Identical opening, different remaining action: keep both occurrences.
    for(std::size_t i=3*432;i<b.sceneSequence.size();++i)b.sceneSequence[i]=float(i%13)/13;
    EXPECT_FALSE(pfcore::MotionMatcher::sameFootageRange(a,0,2,b,50,52));
    EXPECT_FALSE(pfcore::MotionMatcher::sameFootageRange(a,0,20,a,50,52));
    EXPECT_FALSE(pfcore::MotionMatcher::sameFootageRange(a,0,2,b,50,52,[]{return true;}));
    auto x=occurrence(0,2),y=occurrence(50,52);y.descriptor.sceneId=2;
    EXPECT_EQ(pfcore::buildSceneFamilies(std::vector{x,y},{},[](const auto&,const auto&){return true;}).families.size(),1);
    y.descriptor.identityId=8;
    EXPECT_EQ(pfcore::buildSceneFamilies(std::vector{x,y},{},[](const auto&,const auto&){return true;}).families.size(),2);
}

TEST(SceneFamily, HeadOccurrencesUseMeasuredHeadRotationAndHeadEvidence) {
    auto a=occurrence(10,13),b=occurrence(10.2,13.2);
    a.headOnly=b.headOnly=true;a.descriptor.jointEvidence.fill(0);b.descriptor.jointEvidence.fill(0);
    a.descriptor.head.rotation=b.descriptor.head.rotation=std::array<double,9>{1,0,0,0,1,0,0,0,1};
    a.descriptor.evidenceQuality=.01;b.descriptor.evidenceQuality=.1;
    a.descriptor.headEvidenceQuality=.9;b.descriptor.headEvidenceQuality=.5;
    auto families=pfcore::buildSceneFamilies(std::vector{a,b});
    ASSERT_EQ(families.families.size(),1);EXPECT_EQ(families.families[0].representative,0);
    b.descriptor.head.rotation=std::array<double,9>{-1,0,0,0,1,0,0,0,-1};
    EXPECT_EQ(pfcore::buildSceneFamilies(std::vector{a,b}).families.size(),2);
    b.descriptor.head.rotation.reset();EXPECT_EQ(pfcore::buildSceneFamilies(std::vector{a,b}).families.size(),2);
}

TEST(SceneFamily, ProductionOrganizationDeduplicatesPairsAndPreservesDeterministicCanonicalIds) {
    std::vector<pfcore::MotionWindow> windows(2);
    const std::vector<pfcore::Keypoint> pose={{0,-.5,1},{-.1,-.5,1},{.1,-.5,1},{-.2,-.4,1},{.2,-.4,1},
        {-.5,0,1},{.5,0,1},{-.6,.5,1},{.6,.5,1},{-.6,1,1},{.6,1,1},
        {-.3,1,1},{.3,1,1},{-.3,1.8,1},{.3,1.8,1},{-.3,2.6,1},{.3,2.6,1}};
    for(std::size_t i=0;i<2;++i) {
        auto& w=windows[i];w.sourceId=i?"b":"a";w.trackId=1;w.hasSceneIndex=true;w.sceneIndex=1;
        for(int j=0;j<9;++j)w.frames.push_back({10.+i*10+j*.5,pose});
    }
    pfcore::MotionMatch a;a.leftIndex=0;a.rightIndex=1;a.leftSourceId="a";a.rightSourceId="b";
    a.leftStartSeconds=10;a.leftEndSeconds=12.5;a.rightStartSeconds=20;a.rightEndSeconds=22.5;
    a.similarity=.95;a.appearanceVerified=true;
    auto b=a;b.leftStartSeconds+=.2;b.leftEndSeconds+=.2;b.rightStartSeconds+=.2;b.rightEndSeconds+=.2;b.similarity=.99;
    std::vector first{a,b},second{b,a};
    ASSERT_TRUE(pfcore::MotionRanker::organize(first,windows));
    ASSERT_TRUE(pfcore::MotionRanker::organize(second,windows));
    ASSERT_EQ(first.size(),1);ASSERT_EQ(second.size(),1);
    EXPECT_EQ(first[0].leftSceneFamily,second[0].leftSceneFamily);
    EXPECT_EQ(first[0].leftFamilySize,2);
    EXPECT_GT(first[0].evidenceQuality,.5);
    std::vector cancelled{a,b};
    EXPECT_FALSE(pfcore::MotionRanker::organize(cancelled,windows,{},[]{return true;}));
    EXPECT_EQ(cancelled.size(),2);EXPECT_TRUE(cancelled[0].leftSceneFamily.empty());
}
