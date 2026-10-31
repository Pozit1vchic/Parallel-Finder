#include <pfcore/SceneDescriptor.hpp>
#include <gtest/gtest.h>
#include <cmath>
#include <numbers>
#include <limits>

namespace {
pfcore::BodyOrientationSample bodySample(double time,double yaw) {
    pfcore::BodyOrientationSample s;s.sourceId="film";s.modelHash=pfcore::kBodyOrientationModelHash;
    s.time=time;s.yawDegrees=yaw;s.observed=true;s.poseAssociated=true;s.reason="observed";
    s.associationIou=.8;s.associationMargin=.3;s.mirrorDifference=4;s.cropDifference=5;return s;
}
}
TEST(SceneDescriptor, BodyYawPreservesCircularValuesAndDoesNotInventPitchOrHead) {
    pfcore::MotionWindow w;w.sourceId="film";
    w.bodyOrientations={bodySample(0,178),bodySample(.5,-179),bodySample(1,179)};
    const auto d=pfcore::describeScene(w,0,1.1);
    ASSERT_TRUE(d.body.yawDegrees);EXPECT_DOUBLE_EQ(*d.body.yawDegrees,-179);
    EXPECT_FALSE(d.body.pitchDegrees);EXPECT_FALSE(d.head.yawDegrees);
    EXPECT_EQ(d.bodyYawObservedSamples,3);EXPECT_EQ(d.bodyYawRequestedSamples,3);
    EXPECT_GT(d.body.confidence,.5);
    w.sceneStartSeconds=0;w.sceneEndSeconds=1;
    EXPECT_FALSE(pfcore::describeScene(w,0,1.1).body.yawDegrees);
}
TEST(SceneDescriptor, SupportedBodyYawAndIndependentHeadRotationDoNotOverwriteEachOther) {
    pfcore::MotionWindow w;w.sourceId="film";
    w.bodyOrientations={bodySample(0,70),bodySample(.5,70),bodySample(1,70)};
    const std::array<double,9> frontal{1,0,0,0,1,0,0,0,1};
    w.headOrientations={{0,frontal,true},{.5,frontal,true},{1,frontal,true}};
    const auto descriptor=pfcore::describeScene(w,0,1.1);
    ASSERT_TRUE(descriptor.body.yawDegrees);ASSERT_TRUE(descriptor.head.yawDegrees);
    EXPECT_DOUBLE_EQ(*descriptor.body.yawDegrees,70);EXPECT_NEAR(*descriptor.head.yawDegrees,0,1e-9);
    EXPECT_FALSE(descriptor.body.pitchDegrees);
}
TEST(SceneDescriptor, BodyYawRequiresIndependentOwnedTemporalObservations) {
    const std::vector<pfcore::BodyOrientationSample> good{bodySample(0,10),bodySample(.5,20),bodySample(1,30)};
    for(int mode=0;mode<7;++mode) {
        auto samples=good;
        switch(mode) {
        case 0:samples[1].sourceId="bystander";break;
        case 1:samples[1].modelHash="other";break;
        case 2:samples[1].poseAssociated=false;break;
        case 3:samples[1].cropDifference=20.01;break;
        case 4:samples[1].mirrorDifference=25.01;break;
        case 5:samples[1].yawDegrees=std::numeric_limits<double>::quiet_NaN();break;
        case 6:samples[1].associationMargin=.149;break;
        }
        EXPECT_FALSE(pfcore::summarizeBodyYaw(samples,"film",0,1.1).yawDegrees)<<mode;
    }
    auto duplicated=good;duplicated.push_back(good[1]);
    EXPECT_EQ(pfcore::summarizeBodyYaw(duplicated,"film",0,1.1).total,3);
    duplicated.back().yawDegrees=90;
    EXPECT_FALSE(pfcore::summarizeBodyYaw(duplicated,"film",0,1.1).yawDegrees);
    EXPECT_FALSE(pfcore::summarizeBodyYaw(good,"film",0,1).yawDegrees);
    EXPECT_FALSE(pfcore::summarizeBodyYaw(good,"film",0,3).yawDegrees);
}
TEST(SceneDescriptor, BodyYawRejectsTemporalSpikesButAllowsObservedContinuousTurns) {
    auto samples=std::vector{bodySample(0,0),bodySample(.5,160),bodySample(1,0)};
    const auto spike=pfcore::summarizeBodyYaw(samples,"film",0,1.1);
    EXPECT_FALSE(spike.yawDegrees);EXPECT_TRUE(spike.ambiguous);
    samples[1].yawDegrees=90;samples[2].yawDegrees=180;
    const auto turn=pfcore::summarizeBodyYaw(samples,"film",0,1.1);
    ASSERT_TRUE(turn.yawDegrees);EXPECT_DOUBLE_EQ(*turn.yawDegrees,90);
}

TEST(SceneDescriptor, LabelsDoNotChangeContinuousAngularDistances) {
    EXPECT_EQ(pfcore::viewDirection(0),pfcore::ViewDirection::Front);
    EXPECT_EQ(pfcore::viewDirection(45),pfcore::ViewDirection::QuarterLeft);
    EXPECT_EQ(pfcore::viewDirection(-90),pfcore::ViewDirection::SideRight);
    EXPECT_EQ(pfcore::viewDirection(180),pfcore::ViewDirection::Back);
    EXPECT_NEAR(pfcore::angularDistanceDegrees(22.4,22.6),.2,1e-9);
    EXPECT_NEAR(pfcore::angularDistanceDegrees(179,-179),2,1e-9);
}
TEST(SceneDescriptor, MissingJointsRemainUnknownAndNeverLookLikePerfectPoseAgreement) {
    pfcore::MotionWindow window;window.sourceId="film";window.trackId=4;
    window.frames={{0,{{1,2,1}}},{.5,{{2,3,1}}},{1,{{3,4,1}}}};
    const auto descriptor=pfcore::describeScene(window,0,1.1,7);
    EXPECT_EQ(descriptor.identityId,7);
    EXPECT_EQ(descriptor.posture,pfcore::ScenePosture::Unknown);
    EXPECT_FALSE(descriptor.body.yawDegrees);
    EXPECT_FALSE(pfcore::scenePoseDistance(descriptor,descriptor));
    EXPECT_LT(descriptor.evidenceQuality,.1);
}
TEST(SceneDescriptor, MeasuredHeadRotationIsIndependentOfUnknownBodyYaw) {
    pfcore::MotionWindow window;
    const double angle=70.*std::numbers::pi/180.;
    const std::array<double,9> rotation{std::cos(angle),0,std::sin(angle),0,1,0,-std::sin(angle),0,std::cos(angle)};
    window.headOrientations={{.1,rotation,true},{.5,rotation,true},{.9,rotation,true}};
    const auto descriptor=pfcore::describeScene(window,0,1);
    ASSERT_TRUE(descriptor.head.yawDegrees);
    EXPECT_NEAR(*descriptor.head.yawDegrees,70,1e-9);
    EXPECT_FALSE(descriptor.body.yawDegrees);
    EXPECT_FALSE(descriptor.cameraInfluence);
    EXPECT_EQ(descriptor.head.confidence,1);
    ASSERT_TRUE(pfcore::sceneHeadDistance(descriptor,descriptor));
    EXPECT_NEAR(*pfcore::sceneHeadDistance(descriptor,descriptor),0,1e-8);
    auto frontal=descriptor;frontal.head.rotation=std::array<double,9>{1,0,0,0,1,0,0,0,1};
    EXPECT_NEAR(*pfcore::sceneHeadDistance(frontal,descriptor),70./180.,1e-9);
    frontal.head.rotation.reset();EXPECT_FALSE(pfcore::sceneHeadDistance(frontal,descriptor));
    frontal.head.rotation=std::array<double,9>{2,0,0,0,2,0,0,0,2};
    EXPECT_FALSE(pfcore::sceneHeadDistance(frontal,descriptor));
}

TEST(SceneDescriptor, CloseupEvidenceIsMeasuredForTheHeadWithoutClaimingBodyVisibility) {
    pfcore::MotionWindow window;
    std::vector<pfcore::Keypoint> points(17);
    for(auto& point:points)point.confidence=0;
    for(int i=0;i<5;++i)points[i]={.1*i,.1,1};
    for(double time:{0.,.25,.5,.75,1.})window.frames.push_back({time,points});
    const auto d=pfcore::describeScene(window,0,1.1);
    EXPECT_GT(d.headEvidenceQuality,.9);EXPECT_GT(d.headTemporalCoverage,.9);
    EXPECT_EQ(d.visibleCoverage,0);EXPECT_LT(d.evidenceQuality,.2);EXPECT_FALSE(d.body.rollDegrees);
    EXPECT_LT(pfcore::describeScene(window,0,.1).headEvidenceQuality,.2);
    for(auto& frame:window.frames)for(int i=1;i<5;++i)frame.keypoints[i].confidence=0;
    EXPECT_LT(pfcore::describeScene(window,0,1.1).headEvidenceQuality,.2);
}

TEST(SceneDescriptor, BodyRollUsesTheObservedTorsoAndKeepsDepthYawUnknown) {
    pfcore::MotionWindow window;std::vector<pfcore::Keypoint> points(17);
    points[5]={-.5,0,1};points[6]={.5,0,1};
    points[11]={0,.866025403784,1};points[12]={1,.866025403784,1};
    for(double time:{0.,.25,.5,.75})window.frames.push_back({time,points});
    const auto d=pfcore::describeScene(window,0,1);
    ASSERT_TRUE(d.body.rollDegrees);EXPECT_NEAR(*d.body.rollDegrees,30,1e-8);EXPECT_FALSE(d.body.yawDegrees);
}

TEST(SceneDescriptor, CoincidentHighConfidenceJointsAreNotUsablePoseEvidence) {
    pfcore::MotionWindow window;
    for(double time:{0.,.25,.5,.75})window.frames.push_back({time,std::vector<pfcore::Keypoint>(17,{0,0,1})});
    const auto d=pfcore::describeScene(window,0,1);
    EXPECT_LT(d.evidenceQuality,.2);EXPECT_LT(d.headEvidenceQuality,.2);EXPECT_FALSE(d.body.rollDegrees);
}

TEST(SceneDescriptor, VerticalHeadDirectionDoesNotInventFrontalYawOrRoll) {
    for(const double sign:{-1.,1.}) {
        pfcore::MotionWindow window;
        const std::array<double,9> vertical{1,0,0,0,0,-sign,0,sign,0};
        window.headOrientations={{0,vertical,true},{.5,vertical,true},{1,vertical,true}};
        const auto d=pfcore::describeScene(window,0,1.1);
        ASSERT_TRUE(d.head.rotation);ASSERT_TRUE(d.head.pitchDegrees);
        EXPECT_NEAR(*d.head.pitchDegrees,sign*90.,1e-9);
        EXPECT_FALSE(d.head.yawDegrees);EXPECT_FALSE(d.head.rollDegrees);
        EXPECT_TRUE(pfcore::sceneHeadDistance(d,d).has_value());
        EXPECT_NEAR(*pfcore::sceneHeadDistance(d,d),0.,1e-9);
    }
}
