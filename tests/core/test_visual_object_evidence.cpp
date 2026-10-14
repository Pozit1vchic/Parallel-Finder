#include <pfcore/VisualObjectEvidence.hpp>
#include <gtest/gtest.h>
#include <limits>

namespace {
using Cover=pfcore::HeadCovering;
using Phone=pfcore::PhonePosition;
using Phase=pfcore::HoodPhase;
std::vector<pfcore::VisualObjectSample> sequence(std::initializer_list<Cover> states) {
    std::vector<pfcore::VisualObjectSample> result;
    for(const auto state:states) {
        const auto i=result.size();
        result.push_back({1.+i*.2,"source-v1","track-42","visual-v1","decoded-"+std::to_string(i),state,Phone::Absent});
    }
    return result;
}
TEST(VisualObjectEvidence, HoodPhasesRequireStateTransitionAndIndependentHandMotion) {
    auto on=sequence({Cover::None,Cover::None,Cover::Hood,Cover::Hood});
    EXPECT_EQ(pfcore::classifyVisualObjects(on,1,2,true,true).hoodPhase,Phase::PutOn);
    EXPECT_EQ(pfcore::classifyVisualObjects(on,1,2,true,false).hoodPhase,Phase::Unknown);
    EXPECT_EQ(pfcore::classifyVisualObjects(on,1,2,false,true).hoodPhase,Phase::Unknown);
    auto off=sequence({Cover::Hood,Cover::Hood,Cover::None,Cover::None});
    EXPECT_EQ(pfcore::classifyVisualObjects(off,1,2,true,true).hoodPhase,Phase::TakeOff);
    for(const auto& states:{std::vector{Cover::Cap,Cover::Cap,Cover::Cap,Cover::Cap},
        std::vector{Cover::Hood,Cover::Hood,Cover::Hood,Cover::Hood}}) {
        auto held=on;for(std::size_t i=0;i<held.size();++i)held[i].headCovering=states[i];
        EXPECT_EQ(pfcore::classifyVisualObjects(held,1,2,true,true).hoodPhase,Phase::Unknown);
    }
}
TEST(VisualObjectEvidence, RejectsMixedSourcesModelsSubjectsRepeatedFramesAndGaps) {
    const auto valid=sequence({Cover::None,Cover::None,Cover::Hood,Cover::Hood});
    for(int variant=0;variant<6;++variant) {
        auto corrupted=valid;
        if(variant==0)corrupted[2].sourceKey="other-source";
        if(variant==1)corrupted[2].modelKey="other-model";
        if(variant==2)corrupted[2].subjectKey="another-person";
        if(variant==3)corrupted[2].frameKey=corrupted[0].frameKey;
        if(variant==4) {corrupted[2].time+=1;corrupted[3].time+=1;}
        if(variant==5)corrupted[2].time=std::numeric_limits<double>::quiet_NaN();
        const auto result=pfcore::classifyVisualObjects(corrupted,1,4,true,true);
        EXPECT_EQ(result.hoodPhase,Phase::Unknown);EXPECT_EQ(result.phone,Phone::Unknown);
    }
}
TEST(VisualObjectEvidence, DoesNotBridgeUnknownOrReversingStatesOrRangeEnd) {
    auto unknown=sequence({Cover::None,Cover::None,Cover::Unknown,Cover::Hood,Cover::Hood});
    EXPECT_EQ(pfcore::classifyVisualObjects(unknown,1,3,true,true).hoodPhase,Phase::Unknown);
    auto reversing=sequence({Cover::None,Cover::None,Cover::Hood,Cover::None,Cover::Hood,Cover::Hood});
    EXPECT_EQ(pfcore::classifyVisualObjects(reversing,1,3,true,true).hoodPhase,Phase::Unknown);
    auto on=sequence({Cover::None,Cover::None,Cover::Hood,Cover::Hood});
    EXPECT_EQ(pfcore::classifyVisualObjects(on,1,on.back().time,true,true).hoodPhase,Phase::Unknown);
}
TEST(VisualObjectEvidence, PhoneStateIsIndependentOfMotionAndNeverImpliesLookingOrSpeaking) {
    auto samples=sequence({Cover::Cap,Cover::Cap,Cover::Cap,Cover::Cap,Cover::Cap});
    for(auto& sample:samples)sample.phone=Phone::Ear;
    samples[2].phone=Phone::Unknown;
    EXPECT_EQ(pfcore::classifyVisualObjects(samples,1,3,true,false).phone,Phone::Ear);
    samples[3].phone=Phone::Unknown;
    EXPECT_EQ(pfcore::classifyVisualObjects(samples,1,3,true,false).phone,Phone::Unknown);
}
TEST(VisualObjectEvidence, PhoneMotionDistinguishesDirectionFromHeldLocation) {
    using Action=pfcore::PhonePhase;
    auto samples=sequence({Cover::Cap,Cover::Cap,Cover::Cap,Cover::Cap});
    const auto classify=[&] {return pfcore::classifyVisualObjects(samples,1,2,true,true);};
    samples[0].phone=samples[1].phone=Phone::Hand;
    samples[2].phone=samples[3].phone=Phone::Ear;
    EXPECT_EQ(classify().phonePhase,Action::ToEar);
    EXPECT_EQ(classify().phone,Phone::Unknown);
    EXPECT_EQ(classify().hoodPhase,Phase::Unknown);
    for(auto& sample:samples)sample.phone=sample.phone==Phone::Hand?Phone::Ear:Phone::Hand;
    EXPECT_EQ(classify().phonePhase,Action::FromEar);
    EXPECT_EQ(pfcore::classifyVisualObjects(samples,1,2,true,false).phonePhase,Action::Unknown);
    EXPECT_EQ(pfcore::classifyVisualObjects(samples,1,2,false,true).phonePhase,Action::Unknown);
    for(auto& sample:samples)sample.phone=Phone::Ear;
    EXPECT_EQ(classify().phonePhase,Action::Unknown);
    EXPECT_EQ(classify().phone,Phone::Ear);
}
TEST(VisualObjectEvidence, PhoneMotionRequiresVisibleNonReversingEndpoints) {
    using Action=pfcore::PhonePhase;
    auto samples=sequence({Cover::None,Cover::None,Cover::None,Cover::None,Cover::None,Cover::None});
    const std::vector<std::vector<Phone>> invalid{
        {Phone::Absent,Phone::Absent,Phone::Hand,Phone::Ear,Phone::Ear,Phone::Ear},
        {Phone::Hand,Phone::Hand,Phone::Unknown,Phone::Ear,Phone::Ear,Phone::Ear},
        {Phone::Hand,Phone::Hand,Phone::Ear,Phone::Hand,Phone::Ear,Phone::Ear},
        {Phone::Hand,Phone::Ear,Phone::Ear,Phone::Ear,Phone::Ear,Phone::Ear}};
    for(const auto& states:invalid) {
        for(std::size_t i=0;i<samples.size();++i)samples[i].phone=states[i];
        EXPECT_EQ(pfcore::classifyVisualObjects(samples,1,3,true,true).phonePhase,Action::Unknown);
    }
}
}
