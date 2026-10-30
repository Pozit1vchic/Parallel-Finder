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
}
