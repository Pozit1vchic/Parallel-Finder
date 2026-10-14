#include <pfcore/SceneWindow.hpp>
#include <pfcore/MovementClassifier.hpp>
#include <gtest/gtest.h>
#include <stdexcept>
#include <limits>

namespace {
pfcore::MotionWindow cachedShot() {
    pfcore::MotionWindow w;
    w.sourceId="one.mp4";w.trackId=42;w.sceneIndex=7;w.hasSceneIndex=true;
    w.sceneStartSeconds=10;w.sceneEndSeconds=12;
    for(const double t:{10.,10.25,10.5,10.75,11.,11.25,11.5,11.75})
        w.frames.push_back({t,{{t,2,.9}}});
    w.appearanceEmbedding={1,0};w.appearanceConfidence=.9;
    w.faceEmbedding={0,1};w.faceConfidence=.8;
    w.sceneContext={.5};w.sceneView={.3};w.sceneViewSampled=true;
    w.measuredFaces={{10.5,true,.8,.1},{11.5,true,.8,.1}};
    w.directFaceModel="source-face-v1";
    w.directFaces={{10.5,{1,0}},{11.5,{0,1}}};
    w.videoActions.resize(1);w.videoActions[0].sourceId=w.sourceId;
    w.videoActions[0].start=10;w.videoActions[0].end=12;
    w.headOrientations={{10.5,{},true},{11.,{},true},{11.5,{},true}};
    w.sceneSequenceTimes={10.5,11.,11.5};w.sceneSequence.resize(3*432);
    for(std::size_t i=0;i<3;++i)
        std::fill_n(w.sceneSequence.begin()+i*432,432,float(i));
    return w;
}
TEST(SceneWindow, CutFrameBelongsOnlyToTheNextShotAndInputIsUntouched) {
    const auto w=cachedShot();
    const pfcore::ObservedSceneRange scenes[]={{10,11,7},{11,12,99}};
    const auto parts=pfcore::splitMotionWindowAtScenes(w,scenes);
    ASSERT_EQ(parts.size(),2U);
    EXPECT_EQ(parts[0].window.frames.size(),4U);
    EXPECT_EQ(parts[1].window.frames.size(),4U);
    EXPECT_DOUBLE_EQ(parts[0].window.frames.back().timestampSeconds,10.75);
    EXPECT_DOUBLE_EQ(parts[1].window.frames.front().timestampSeconds,11);
    EXPECT_EQ(parts[0].window.sceneIndex,7U);EXPECT_EQ(parts[1].window.sceneIndex,99U);
    EXPECT_EQ(parts[0].window.trackId,42U);EXPECT_EQ(parts[1].window.trackId,42U);
    EXPECT_EQ(w.frames.size(),8U);EXPECT_EQ(w.faceEmbedding,(std::vector<float>{0,1}));
    EXPECT_EQ(parts[0].window.sceneSequenceTimes,(std::vector<double>{10.5}));
    EXPECT_EQ(parts[1].window.sceneSequenceTimes,(std::vector<double>{11,11.5}));
    EXPECT_FLOAT_EQ(parts[1].window.sceneSequence[0],1);
    EXPECT_EQ(parts[0].window.headOrientations.size(),1U);
    ASSERT_EQ(parts[1].window.headOrientations.size(),2U);
    EXPECT_DOUBLE_EQ(parts[1].window.headOrientations.front().time,11.);
    for(const auto& part:parts) {
        EXPECT_TRUE(part.window.directFaces.empty());
        EXPECT_TRUE(part.window.directFaceModel.empty());
        EXPECT_TRUE(part.window.videoActions.empty());
    }
    EXPECT_EQ(w.directFaces.size(),2U);
    EXPECT_EQ(w.videoActions.size(),1U);
    EXPECT_EQ(w.headOrientations.size(),3U);
}
TEST(SceneWindow, InvalidatesMixedShotEvidenceEvenForAChunkBeforeTheCut) {
    auto w=cachedShot();w.frames.resize(4);
    const pfcore::ObservedSceneRange scenes[]={{10,11,7},{11,12,99}};
    const auto parts=pfcore::splitMotionWindowAtScenes(w,scenes);
    ASSERT_EQ(parts.size(),1U);
    EXPECT_TRUE(parts[0].identityNeedsRefresh);
    EXPECT_TRUE(parts[0].window.appearanceEmbedding.empty());
    EXPECT_TRUE(parts[0].window.faceEmbedding.empty());
    EXPECT_DOUBLE_EQ(parts[0].window.faceConfidence,0);
    EXPECT_DOUBLE_EQ(parts[0].window.appearanceConfidence,0);
    EXPECT_TRUE(parts[0].window.measuredFaces.empty());
    EXPECT_TRUE(parts[0].window.directFaces.empty());
    EXPECT_TRUE(parts[0].window.directFaceModel.empty());
    EXPECT_TRUE(parts[0].window.sceneContext.empty());
    EXPECT_TRUE(parts[0].window.sceneView.empty());
    EXPECT_FALSE(parts[0].window.sceneViewSampled);
}
TEST(SceneWindow, UnchangedShotKeepsItsMeasuredIdentityAndViews) {
    const auto w=cachedShot();const pfcore::ObservedSceneRange scene{10,12,7};
    const auto parts=pfcore::splitMotionWindowAtScenes(w,std::span(&scene,1));
    ASSERT_EQ(parts.size(),1U);EXPECT_FALSE(parts[0].identityNeedsRefresh);
    EXPECT_EQ(parts[0].window.faceEmbedding,w.faceEmbedding);
    EXPECT_EQ(parts[0].window.appearanceEmbedding,w.appearanceEmbedding);
    EXPECT_EQ(parts[0].window.sceneContext,w.sceneContext);
    EXPECT_EQ(parts[0].window.sceneView,w.sceneView);
    EXPECT_EQ(parts[0].window.measuredFaces.size(),2U);
    EXPECT_EQ(parts[0].window.videoActions.size(),1U);
    EXPECT_EQ(parts[0].window.directFaceModel,w.directFaceModel);
    EXPECT_EQ(parts[0].window.directFaces.size(),w.directFaces.size());
    EXPECT_EQ(parts[0].window.headOrientations.size(),w.headOrientations.size());
}
TEST(SceneWindow, SourcePartitionSharesSceneIdsAcrossTracksAndChunks) {
    const auto original=cachedShot();
    auto early=original;early.frames.resize(4);
    auto late=original;late.frames.erase(late.frames.begin(),late.frames.begin()+4);late.trackId=81;
    auto unchanged=original;unchanged.sceneStartSeconds=20;unchanged.sceneEndSeconds=22;unchanged.sceneIndex=100;
    for(auto& f:unchanged.frames)f.timestampSeconds+=10;
    const std::vector<pfcore::MotionWindow> input{late,unchanged,early};
    const double cuts[]{11,11};
    const auto parts=pfcore::splitSourceWindowsAtCuts(input,cuts);
    ASSERT_EQ(parts.size(),3U);
    EXPECT_EQ(parts[0].window.sceneIndex,102U);
    EXPECT_EQ(parts[2].window.sceneIndex,101U);
    EXPECT_TRUE(parts[0].identityNeedsRefresh);EXPECT_TRUE(parts[2].identityNeedsRefresh);
    EXPECT_FALSE(parts[1].identityNeedsRefresh);EXPECT_EQ(parts[1].window.sceneIndex,100U);
    EXPECT_EQ(parts[1].window.faceEmbedding,unchanged.faceEmbedding);
    const std::vector<pfcore::MotionWindow> reordered{early,late,unchanged};
    const auto other=pfcore::splitSourceWindowsAtCuts(reordered,cuts);
    EXPECT_EQ(other[0].window.sceneIndex,parts[2].window.sceneIndex);
    EXPECT_EQ(other[1].window.sceneIndex,parts[0].window.sceneIndex);
    EXPECT_EQ(input[0].directFaces.size(),2U);
}
TEST(SceneWindow, SourcePartitionRejectsCrossSourceCutsAndInvalidTimes) {
    auto a=cachedShot(),b=a;b.sourceId="other.mp4";
    const double cut[]{11};
    EXPECT_THROW(pfcore::splitSourceWindowsAtCuts(std::vector{a,b},cut),std::invalid_argument);
    const double reversed[]{11,10.5};
    EXPECT_THROW(pfcore::splitSourceWindowsAtCuts(std::span(&a,1),reversed),std::invalid_argument);
    const double invalid[]{std::numeric_limits<double>::quiet_NaN()};
    EXPECT_THROW(pfcore::splitSourceWindowsAtCuts(std::span(&a,1),invalid),std::invalid_argument);
    const double endpoints[]{10,12};
    const auto unchanged=pfcore::splitSourceWindowsAtCuts(std::span(&a,1),endpoints);
    ASSERT_EQ(unchanged.size(),1U);EXPECT_FALSE(unchanged[0].identityNeedsRefresh);
}
TEST(SceneWindow, RejectsGapsOverlapsIncompleteCoverageAndDuplicateShotIds) {
    const auto w=cachedShot();
    for(const std::vector<pfcore::ObservedSceneRange> scenes:{
        std::vector<pfcore::ObservedSceneRange>{{10,10.9,7},{11,12,99}},
        {{10,11.1,7},{11,12,99}},{{10,11,7}},{{10,11,7},{11,12,7}}})
        EXPECT_THROW(pfcore::splitMotionWindowAtScenes(w,scenes),std::invalid_argument);
    auto invalid=w;invalid.frames[1].timestampSeconds=invalid.frames[0].timestampSeconds;
    const pfcore::ObservedSceneRange scene{10,12,7};
    EXPECT_THROW(pfcore::splitMotionWindowAtScenes(invalid,std::span(&scene,1)),std::invalid_argument);
}
}
