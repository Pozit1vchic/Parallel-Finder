#include <pfcore/FaceConsensus.hpp>
#include <gtest/gtest.h>
#include <algorithm>
#include <limits>
namespace {
std::vector<pfcore::DirectFaceSample> faces(double offset,std::vector<float> embedding)
{return {{offset,embedding},{offset+.2,embedding},{offset+.4,embedding}};}
}
TEST(FaceConsensus, RepeatedModerateMatchesSupportBothIndependentTimelines)
{
    auto a=faces(10,{1,0}),b=faces(100,{.5F,.866F});
    auto result=pfcore::compareDirectFaces(a,10,11,b,100,101);
    EXPECT_EQ(result.decision,pfcore::FaceConsensusDecision::Supported);
    EXPECT_EQ(result.leftSupported,3U);EXPECT_EQ(result.rightSupported,3U);
    EXPECT_NEAR(result.meanSupportedSimilarity,.5,1e-4);
}
TEST(FaceConsensus, MissingDuplicateAndOutOfRangeEvidenceCannotConfirm)
{
    auto a=faces(10,{1,0}),b=faces(100,{1,0});
    b[1].timestampSeconds=b[2].timestampSeconds=100;
    EXPECT_EQ(pfcore::compareDirectFaces(a,10,11,b,100,101).decision,pfcore::FaceConsensusDecision::Insufficient);
    b=faces(100,{1,0});
    EXPECT_EQ(pfcore::compareDirectFaces(a,10,11,b,101,102).decision,pfcore::FaceConsensusDecision::Insufficient);
    b[1].embedding[0]=std::numeric_limits<float>::quiet_NaN();
    EXPECT_EQ(pfcore::compareDirectFaces(a,10,11,b,100,101).decision,pfcore::FaceConsensusDecision::Insufficient);
}
TEST(FaceConsensus, ContradictoryFaceCannotHideInPositiveAverage)
{
    auto a=faces(10,{1,0}),b=faces(100,{1,0});
    b.push_back({100.6,{0,1}});
    EXPECT_EQ(pfcore::compareDirectFaces(a,10,11,b,100,101).decision,pfcore::FaceConsensusDecision::Contradicted);
    b=faces(100,{0,1});
    EXPECT_EQ(pfcore::compareDirectFaces(a,10,11,b,100,101).decision,pfcore::FaceConsensusDecision::Contradicted);
}
TEST(FaceConsensus, DifferentEmbeddingContractsDoNotBecomeNegativeIdentity)
{
    auto a=faces(10,{1,0}),b=faces(100,{1,0,0});
    EXPECT_EQ(pfcore::compareDirectFaces(a,10,11,b,100,101).decision,pfcore::FaceConsensusDecision::Insufficient);
}
TEST(FaceConsensus, ContradictoryDuplicateTimestampCannotHideBehindFirstRecord)
{
    auto a=faces(10,{1,0}),b=faces(100,{1,0});
    b.push_back({100.2,{0,1}});
    EXPECT_EQ(pfcore::compareDirectFaces(a,10,11,b,100,101).decision,pfcore::FaceConsensusDecision::Insufficient);
    std::reverse(b.begin(),b.end());
    EXPECT_EQ(pfcore::compareDirectFaces(a,10,11,b,100,101).decision,pfcore::FaceConsensusDecision::Insufficient);
}
