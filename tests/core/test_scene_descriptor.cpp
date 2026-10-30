#include <pfcore/SceneDescriptor.hpp>
#include <gtest/gtest.h>
#include <cmath>
#include <numbers>

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
