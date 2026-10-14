#include <pfcore/HeadOrientation.hpp>
#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

namespace {
using Matrix=std::array<double,9>;
Matrix multiply(const Matrix& a,const Matrix& b) {
    Matrix result{};
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)for(int k=0;k<3;++k)
        result[i*3+j]+=a[i*3+k]*b[k*3+j];
    return result;
}
Matrix axisRotation(int axis,double degrees) {
    const double angle=degrees*std::numbers::pi/180.,c=std::cos(angle),s=std::sin(angle);
    if(axis==0)return {1,0,0,0,c,-s,0,s,c};
    if(axis==1)return {c,0,s,0,1,0,-s,0,c};
    return {c,-s,0,s,c,0,0,0,1};
}
template<class Action> auto trajectory(Action action,const Matrix& initial=axisRotation(0,0)) {
    std::vector<pfcore::HeadOrientationSample> result;
    for(int i=0;i<=20;++i) {
        const double t=i/20.;
        result.push_back({t*2,multiply(action(t),initial),true});
    }
    return result;
}
auto classify(const auto& samples) {return pfcore::classifyHeadOrientation(samples,0,2).gesture;}

TEST(HeadOrientation, SeparatesTurnTiltAndReturningNodAcrossStartingPoses) {
    for(const double initialYaw:{0.,70.,170.})for(const double sign:{-1.,1.}) {
        const auto initial=multiply(axisRotation(1,initialYaw),axisRotation(0,25));
        EXPECT_EQ(classify(trajectory([&](double t){return axisRotation(1,sign*40*t);},initial)),pfcore::GestureClass::HeadTurn);
        for(int axis:{0,2}) EXPECT_EQ(classify(trajectory([&](double t){return axisRotation(axis,sign*30*t);},initial)),pfcore::GestureClass::HeadTilt);
        EXPECT_EQ(classify(trajectory([&](double t){return axisRotation(0,sign*30*std::sin(std::numbers::pi*t));},initial)),pfcore::GestureClass::HeadNod);
    }
}
TEST(HeadOrientation, CompoundMovementAndNoiseAreNotSingleAxisActions) {
    EXPECT_EQ(classify(trajectory([](double t){return multiply(axisRotation(1,30*t),axisRotation(0,25*t));})),pfcore::GestureClass::Mixed);
    EXPECT_EQ(classify(trajectory([](double t){return multiply(axisRotation(1,2*std::sin(30*t)),axisRotation(0,2*std::cos(20*t)));})),pfcore::GestureClass::Static);
    EXPECT_EQ(classify(trajectory([](double t){return axisRotation(1,10*t);})),pfcore::GestureClass::Unknown);
}
TEST(HeadOrientation, ExclusiveClipEndDoesNotIncludeTheNextFrame) {
    auto samples=trajectory([](double){return axisRotation(1,0);});
    // The frame at exactly 2 s is outside a clip ending at 2 s. It can
    // belong to another crop or shot and must not invalidate the held head.
    samples.back().rotation=axisRotation(1,90);
    EXPECT_EQ(pfcore::classifyHeadOrientation(samples,0,2).gesture,pfcore::GestureClass::Static);
    EXPECT_EQ(pfcore::classifyHeadPoseOrientation(samples,0,2),"head_front");
    // Extending the interval actually includes the discontinuity, which is
    // still rejected rather than explained as a legitimate head movement.
    EXPECT_EQ(pfcore::classifyHeadOrientation(samples,0,2.01).gesture,pfcore::GestureClass::Unknown);
}
TEST(HeadOrientation, CompoundActionsRetainTurnAndTiltOrNodFacets) {
    using G=pfcore::GestureClass;
    for(bool returning:{false,true}) {
        const auto source=trajectory([&](double t) {
            return multiply(axisRotation(1,45*t),axisRotation(0,returning ? 30*std::sin(std::numbers::pi*t):25*t));
        });
        const auto result=pfcore::classifyHeadOrientation(source,0,2);
        EXPECT_EQ(result.gesture,G::Mixed);
        EXPECT_EQ(result.actions,(std::vector<G>{G::HeadTurn,returning ? G::HeadNod:G::HeadTilt}));
        EXPECT_EQ(pfcore::classifyHeadTurnPhase(source,0,2),"head_turn_away");
    }
    const auto noise=trajectory([](double t){return multiply(axisRotation(1,30*t),axisRotation(0,2*std::sin(20*t)));});
    EXPECT_TRUE(pfcore::classifyHeadOrientation(noise,0,2).actions.empty());
}
TEST(HeadOrientation, RejectsInvalidRotationsDiscontinuousDetectionsAndInsufficientCoverage) {
    const auto original=trajectory([](double t){return axisRotation(1,40*t);});
    auto source=original;
    for(int i=0;i<6;++i)source[i].observed=false;
    EXPECT_EQ(classify(source),pfcore::GestureClass::Unknown);
    source=original;
    for(int i=5;i<15;++i)source[i].observed=false;
    EXPECT_EQ(classify(source),pfcore::GestureClass::Unknown);
    source=original;source[10].time=source[9].time;
    EXPECT_EQ(classify(source),pfcore::GestureClass::Unknown);
    source=original;for(auto& s:source)s.rotation[0]=std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(classify(source),pfcore::GestureClass::Unknown);
    source=original;for(auto& s:source)s.rotation[8]*=-1;
    EXPECT_EQ(classify(source),pfcore::GestureClass::Unknown);
    for(double jump:{80.,180.}) {
        source=original;source[10].rotation=axisRotation(1,jump);
        EXPECT_EQ(classify(source),pfcore::GestureClass::Unknown);
    }
    source=original;source.erase(source.begin()+5,source.begin()+12);
    EXPECT_EQ(classify(source),pfcore::GestureClass::Unknown);
}
TEST(HeadOrientation, RestrictsEvidenceToTheRequestedTrim) {
    const auto source=trajectory([](double t){return axisRotation(1,t<.5 ? 0:70*(t-.5));});
    EXPECT_EQ(pfcore::classifyHeadOrientation(source,0,.8).gesture,pfcore::GestureClass::Static);
    EXPECT_EQ(pfcore::classifyHeadOrientation(source,1.2,2).gesture,pfcore::GestureClass::HeadTurn);
    EXPECT_EQ(pfcore::classifyHeadOrientation(source,0,4).gesture,pfcore::GestureClass::Unknown);
}
TEST(HeadOrientation, ObservedPoseSeparatesFrontProfileAndUncertainOrientation) {
    for(double yaw:{-70.,70.}) {
        const auto source=trajectory([&](double){return axisRotation(1,yaw);});
        EXPECT_EQ(pfcore::classifyHeadPoseOrientation(source,0,2),"head_turned");
    }
    for(double yaw:{-15.,15.}) {
        const auto source=trajectory([&](double){return axisRotation(1,yaw);});
        EXPECT_EQ(pfcore::classifyHeadPoseOrientation(source,0,2),"head_front");
    }
    for(double yaw:{28.,160.,-160.}) {
        const auto source=trajectory([&](double){return axisRotation(1,yaw);});
        EXPECT_EQ(pfcore::classifyHeadPoseOrientation(source,0,2),"head_pose");
    }
    auto source=trajectory([](double){return axisRotation(1,70);});
    for(int i=0;i<6;++i)source[i].observed=false;
    EXPECT_EQ(pfcore::classifyHeadPoseOrientation(source,0,2),"head_pose");
}
TEST(HeadOrientation, TurnPhaseUsesCameraRelativeEndpointsAndPreservesUncertainty) {
    for(double sign:{-1.,1.}) {
        const auto away=trajectory([&](double t){return axisRotation(1,sign*60*t);});
        const auto back=trajectory([&](double t){return axisRotation(1,sign*60*(1-t));});
        EXPECT_EQ(pfcore::classifyHeadTurnPhase(away,0,2),"head_turn_away");
        EXPECT_EQ(pfcore::classifyHeadTurnPhase(back,0,2),"head_turn_back");
        // Trimming away the frontal endpoint removes the specific phase.
        EXPECT_EQ(pfcore::classifyHeadTurnPhase(away,.9,2),"head_turn");
        const auto profile=trajectory([&](double t){return axisRotation(1,sign*(40+40*t));});
        EXPECT_EQ(pfcore::classifyHeadTurnPhase(profile,0,2),"head_turn");
        const auto returning=trajectory([&](double t){return axisRotation(1,sign*50*std::sin(std::numbers::pi*t));});
        EXPECT_EQ(pfcore::classifyHeadTurnPhase(returning,0,2),"head_turn");
    }
    auto insufficient=trajectory([](double t){return axisRotation(1,60*t);});
    for(int i=0;i<6;++i)insufficient[i].observed=false;
    EXPECT_EQ(pfcore::classifyHeadTurnPhase(insufficient,0,2),"head_turn");
    const auto pitched=trajectory([](double t){return multiply(axisRotation(1,60*t),axisRotation(0,60));});
    EXPECT_EQ(pfcore::classifyHeadTurnPhase(pitched,0,2),"head_turn");
    const auto behind=trajectory([](double t){return axisRotation(1,130+40*t);});
    EXPECT_EQ(pfcore::classifyHeadTurnPhase(behind,0,2),"head_turn");
}
}
