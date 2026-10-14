#include <gtest/gtest.h>
#include <pfcore/VideoActionEvidence.hpp>
#include <pfcore/MovementClassifier.hpp>
#include <limits>

namespace {
pfcore::VideoActionEvidence measuredClip() {
    pfcore::VideoActionEvidence result;
    result.sourceId="selected-source";result.modelHash=pfcore::kVideoActionModelHash;
    result.start=0;result.end=1.5;
    result.observed=result.continuous=result.associated=result.torsoVisible=true;
    for(int i=0;i<15;++i)result.frameTimes.push_back(i*.1);
    result.scores[13]=.99;result.frozenScores[13]=.02;
    return result;
}
}
TEST(VideoActionEvidence, RequiresMeasuredMovementOfTheSelectedContinuousSubject) {
    const auto valid=measuredClip();
    ASSERT_TRUE(pfcore::measuredWalking(valid,"selected-source",0,1.5));
    for(int invalid=0;invalid<12;++invalid) {
        auto changed=valid;
        switch(invalid) {
        case 0:changed.sourceId="another-person-source";break;
        case 1:changed.associated=false;break;
        case 2:changed.continuous=false;break;
        case 3:changed.torsoVisible=false;break;
        case 4:changed.modelHash="shape-compatible-untrained-model";break;
        case 5:changed.frameTimes.back()=1.5;break;
        case 6:changed.frameTimes[3]=changed.frameTimes[2];break;
        case 7:changed.scores[5]=std::numeric_limits<double>::quiet_NaN();break;
        case 8:changed.frozenScores[13]=.8;break;
        case 9:changed.scores[13]=.8;break;
        case 10:changed.scores[11]=.95;break;
        case 11:changed.scores[9]=.95;break;
        }
        EXPECT_FALSE(pfcore::measuredWalking(changed,"selected-source",0,1.5))<<invalid;
    }
    EXPECT_FALSE(pfcore::measuredWalking(valid,"selected-source",.1,1.5));
    auto gap=valid;gap.frameTimes.erase(gap.frameTimes.begin()+3,gap.frameTimes.begin()+7);
    EXPECT_FALSE(pfcore::measuredWalking(gap,"selected-source",0,1.5));
}
TEST(VideoActionEvidence, AddsValidatedWalkingToUnknownGeometryWithoutChangingHeadScope) {
    pfcore::MotionWindow window;window.sourceId="selected-source";
    window.sceneStartSeconds=0;window.sceneEndSeconds=2;
    const pfcore::MovementClassifier classifier;
    EXPECT_EQ(classifier.classifyForGrouping(window,0,1.5).gesture,pfcore::GestureClass::Unknown);
    window.videoActions.push_back(measuredClip());
    const auto action=classifier.classifyForGrouping(window,0,1.5);
    EXPECT_EQ(action.gesture,pfcore::GestureClass::Step);
    EXPECT_EQ(action.actions,(std::vector<pfcore::GestureClass>{pfcore::GestureClass::Step}));
    EXPECT_EQ(classifier.classifyForGrouping(window,0,1.5,true).gesture,pfcore::GestureClass::Unknown);
    EXPECT_EQ(classifier.classifyForGrouping(window,.1,1.5).gesture,pfcore::GestureClass::Unknown);
    EXPECT_EQ(pfcore::MovementClassifier::combineForGrouping(action,{}).gesture,pfcore::GestureClass::Unknown);
    EXPECT_EQ(pfcore::MovementClassifier::combineForGrouping(action,action).gesture,pfcore::GestureClass::Step);
}
TEST(VideoActionEvidence, GeometricLabelsCannotBorrowAnUnobservedClipTail) {
    pfcore::MotionWindow window;
    const std::vector<pfcore::Keypoint> pose={{0,-2},{-.5,-2.5},{.5,-2.5},{-1,-2},{1,-2},
        {-2,0},{2,0},{-2,1.5},{2,1.5},{-2,3},{2,3},{-1,4},{1,4},{-1,6},{1,6},{-1,8},{1,8}};
    for(int i=0;i<=10;++i)window.frames.push_back({i*.1,pose});
    const pfcore::MovementClassifier classifier;
    EXPECT_EQ(classifier.classifyForGrouping(window,0,1).gesture,pfcore::GestureClass::Static);
    EXPECT_EQ(classifier.classifyForGrouping(window,0,2).gesture,pfcore::GestureClass::Unknown);
}
