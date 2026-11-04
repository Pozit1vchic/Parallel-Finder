#include <pfcore/DominantPerson.hpp>
#include <pfcore/FaceAssociation.hpp>
#include <gtest/gtest.h>
#include <limits>

namespace {
pfcore::PersonTrack track(std::size_t id, double start, std::vector<float> face, int count = 4)
{
    pfcore::PersonTrack result;
    result.id = id;
    for (int i = 0; i < count; ++i) {
        pfcore::PersonDetection observation;
        observation.timestampSeconds = start + i * .25;
        observation.frameDurationSeconds = .25;
        observation.faceEmbedding = face;
        observation.appearanceEmbedding = face;
        observation.keypointConfidence = .8;
        result.observations.push_back(observation);
    }
    return result;
}
}

TEST(CharacterCandidates, RetainsMinorityTargetAndStableTrackIds)
{
    const auto candidates = pfcore::characterCandidates({track(4,0,{1,0},40), track(7,12,{0,1})});
    ASSERT_EQ(candidates.size(),2);
    EXPECT_EQ(candidates[0].id,4);
    EXPECT_EQ(candidates[1].id,7);
    EXPECT_DOUBLE_EQ(candidates[1].presenceSeconds,1);
    EXPECT_EQ(candidates[1].observationCount,4);
    EXPECT_GT(candidates[1].appearances.size(),1);
    EXPECT_TRUE(candidates[0].distinctIdentity);
    EXPECT_TRUE(candidates[1].distinctIdentity);
}

TEST(CharacterCandidates, SelectorLimitsOnlyUnresolvedAndKeepsSuggestedTarget)
{
    std::vector<pfcore::CharacterCandidate> people(27);
    for(std::size_t i=0;i<people.size();++i) {
        people[i].id=i+1;people[i].presenceSeconds=static_cast<double>(i);
        people[i].distinctIdentity=i<2;
    }
    const auto order=pfcore::characterSelectionOrder(people,20,3);
    ASSERT_EQ(order.size(),22);EXPECT_TRUE(people[order[0]].distinctIdentity);
    EXPECT_TRUE(people[order[1]].distinctIdentity);
    EXPECT_EQ(people[order[2]].id,3); // suggested despite its short presence
    EXPECT_EQ(std::count_if(order.begin(),order.end(),[&](auto i){return !people[i].distinctIdentity;}),20);
    EXPECT_EQ(order[3],26); // remaining unresolved entries start with the longest
    EXPECT_EQ(pfcore::characterSelectionOrder(people,0).size(),2);
}

TEST(CharacterCandidates, AmbiguousDisconnectedAppearanceIsNotAnotherUniquePerson)
{
    const auto candidates=pfcore::characterCandidates({track(1,0,{1,0}),track(2,2,{.25F,.9682458F})});
    ASSERT_EQ(candidates.size(),2);
    EXPECT_TRUE(candidates[0].distinctIdentity);
    EXPECT_FALSE(candidates[1].distinctIdentity);
    EXPECT_EQ(candidates[1].trackIds,(std::vector<std::size_t>{2}));
}

TEST(CharacterCandidates, RepresentativeSamplingRetainsAVisibleFaceBetweenUniformSamples)
{
    auto person=track(1,0,{},8);
    for(auto& observation:person.observations) {
        observation.appearanceEmbedding={1,0};observation.keypointConfidence=.95;
    }
    person.observations[2].faceEmbedding={1,0};person.observations[2].keypointConfidence=.6;
    const auto candidates=pfcore::characterCandidates({person});
    ASSERT_EQ(candidates.size(),1);
    const auto& previews=candidates.front().appearances;
    ASSERT_EQ(previews.size(),5);
    const auto representative=std::max_element(previews.begin(),previews.end(),
        [](const auto& a,const auto& b){return a.quality<b.quality;});
    EXPECT_DOUBLE_EQ(representative->timestampSeconds,.5);
    EXPECT_TRUE(std::is_sorted(previews.begin(),previews.end(),
        [](const auto& a,const auto& b){return a.timestampSeconds<b.timestampSeconds;}));
}

TEST(CharacterCandidates, RepeatedSeparatePeopleEstablishDistinctnessWithoutStrongFaceContradiction)
{
    auto a=track(1,0,{1,0}),b=track(2,0,{.25F,.9682458F});
    for(auto& observation:a.observations)observation.box={0,0,100,200};
    for(auto& observation:b.observations)observation.box={200,0,300,200};
    const auto candidates=pfcore::characterCandidates({a,b});
    ASSERT_EQ(candidates.size(),2);
    EXPECT_TRUE(candidates[0].distinctIdentity);
    EXPECT_TRUE(candidates[1].distinctIdentity);
    // Keep a valid four-observation track, but only one shared frame.
    for (std::size_t i=1;i<b.observations.size();++i)
        b.observations[i].timestampSeconds=2+i*.25;
    const auto brief=pfcore::characterCandidates({a,b});
    ASSERT_EQ(brief.size(),2);
    EXPECT_FALSE(brief[1].distinctIdentity);
}

TEST(CharacterCandidates, AMeasuredFaceCannotBeBorrowedByAnOverlappingBystander) {
    const pfcore::ObservedFaceLandmarks face{true,{100,100,140,100,120,120,105,140,135,140}};
    pfcore::PersonDetection actual,back;
    actual.faceEmbedding=back.faceEmbedding={1,0};
    actual.keypoints={{120,120,1},{100,100,1},{140,100,1}};
    back.keypoints={{110,123,1},{75,100,1},{110,100,1}};
    std::vector people{back,actual};pfcore::enforceUniqueFaceOwnership(people,std::vector{face,face});
    EXPECT_TRUE(people[0].faceEmbedding.empty());EXPECT_FALSE(people[1].faceEmbedding.empty());
    people={actual,actual};pfcore::enforceUniqueFaceOwnership(people,std::vector{face,face});
    EXPECT_TRUE(people[0].faceEmbedding.empty());EXPECT_TRUE(people[1].faceEmbedding.empty());
    auto other=face;for(std::size_t i=0;i<10;i+=2)other.points[i]+=200;
    auto otherPerson=actual;for(auto& point:otherPerson.keypoints)point.x+=200;
    people={actual,otherPerson};pfcore::enforceUniqueFaceOwnership(people,std::vector{face,other});
    EXPECT_FALSE(people[0].faceEmbedding.empty());EXPECT_FALSE(people[1].faceEmbedding.empty());
}

TEST(CharacterCandidates, AnUncontestedFaceStillRequiresAgreementWithTheObservedHead) {
    const pfcore::ObservedFaceLandmarks face{true,{100,100,140,100,120,120,105,140,135,140}};
    pfcore::PersonDetection person;person.faceEmbedding={1,0};
    person.keypoints={{120,120,1},{20,100,1},{60,100,1}};
    std::vector people{person};pfcore::enforceUniqueFaceOwnership(people,std::vector{face});
    EXPECT_TRUE(people[0].faceEmbedding.empty());
    person.keypoints={{120,120,1},{100,100,1},{140,100,1}};
    people={person};pfcore::enforceUniqueFaceOwnership(people,std::vector{face});
    EXPECT_FALSE(people[0].faceEmbedding.empty());
}

TEST(CharacterCandidates, ChainedDuplicateFacesCannotHaveMultipleOwnersInAnyOrder) {
    const pfcore::ObservedFaceLandmarks face{true,{100,100,140,100,120,120,105,140,135,140}};
    auto middle=face,last=face;
    for(std::size_t i=0;i<10;i+=2) { middle.points[i]+=6; last.points[i]+=12; }
    pfcore::PersonDetection person;person.faceEmbedding={1,0};
    person.keypoints={{120,120,1},{100,100,1},{140,100,1}};
    std::vector<pfcore::ObservedFaceLandmarks> landmarks{face,middle,last};
    std::vector<int> order{0,1,2};
    do {
        std::vector<pfcore::PersonDetection> people(3,person);
        std::vector<pfcore::ObservedFaceLandmarks> ordered;
        for(const auto i:order) ordered.push_back(landmarks[i]);
        pfcore::enforceUniqueFaceOwnership(people,ordered);
        EXPECT_EQ(std::count_if(people.begin(),people.end(),[](const auto& p){return !p.faceEmbedding.empty();}),1);
        EXPECT_FALSE(people[std::find(order.begin(),order.end(),0)-order.begin()].faceEmbedding.empty());
    } while(std::next_permutation(order.begin(),order.end()));
}

TEST(CharacterCandidates, IdentitySwitchCannotCreateAFalseCoVisibilityVetoForTheSameLead) {
    pfcore::PersonTracker tracker(.30,1.,true,.65);
    pfcore::PersonDetection lead;lead.box={0,0,100,200};lead.confidence=.95;
    lead.keypointConfidence=.8;lead.faceEmbedding={1,0};lead.appearanceEmbedding={1,0};
    for(double time:{0.,.25,.5,.75})tracker.update(time,.25,{lead});
    auto bystander=lead;bystander.faceEmbedding.clear();bystander.appearanceEmbedding={.5F,.8660254F};
    auto returning=lead;returning.box={300,0,400,200};
    for(double time:{1.,1.25,1.5})tracker.update(time,.25,{bystander,returning});
    const auto characters=pfcore::characterCandidates(tracker.tracks());
    ASSERT_EQ(characters.size(),2);
    const auto found=std::find_if(characters.begin(),characters.end(),[](const auto& c){return !c.prototype.face.empty();});
    ASSERT_NE(found,characters.end());EXPECT_EQ(found->trackIds.size(),2);
    EXPECT_EQ(found->observationCount,7);
    EXPECT_THROW(pfcore::PersonTracker(.3,1.,true,1.1),std::invalid_argument);
}

TEST(CharacterCandidates, ReusesFaceVetoAndCoVisibility)
{
    auto a = track(1,0,{1,0}), b = track(2,2,{1,0}), other = track(3,4,{0,1});
    const auto candidates = pfcore::characterCandidates({a,b,other});
    ASSERT_EQ(candidates.size(),2);
    EXPECT_EQ(candidates[0].trackIds,(std::vector<std::size_t>{1,2}));
    EXPECT_EQ(pfcore::characterCandidates({a,track(2,0,{1,0})}).size(),2);
}

TEST(CharacterCandidates, RepeatedObservedViewsRecoverAProfileRejectedByTheMean)
{
    auto front=track(1,0,{.7660444F,.6427876F},6);
    for(std::size_t i=3;i<front.observations.size();++i)
        front.observations[i].faceEmbedding={.7660444F,-.6427876F};
    auto profile=track(2,3,{.3420201F,.9396926F});
    for(auto* person:{&front,&profile})for(auto& o:person->observations)o.appearanceEmbedding={1,0};
    const auto candidates=pfcore::characterCandidates({front,profile});
    ASSERT_EQ(candidates.size(),1);
    EXPECT_EQ(candidates.front().trackIds,(std::vector<std::size_t>{1,2}));
    EXPECT_EQ(candidates.front().observationCount,10);
}

TEST(CharacterCandidates, OneMatchingImageOrRepeatedTimestampCannotRecoverIdentity)
{
    auto front=track(1,0,{.7660444F,-.6427876F},6);
    front.observations[0].faceEmbedding={.7660444F,.6427876F};
    auto profile=track(2,3,{.3420201F,.9396926F});
    for(auto* person:{&front,&profile})for(auto& o:person->observations)o.appearanceEmbedding={1,0};
    EXPECT_EQ(pfcore::characterCandidates({front,profile}).size(),2);
    front.observations[1].faceEmbedding=front.observations[0].faceEmbedding;
    front.observations[1].timestampSeconds=front.observations[0].timestampSeconds;
    EXPECT_EQ(pfcore::characterCandidates({front,profile}).size(),2);
}

TEST(CharacterCandidates, DuplicateFrameDoesNotIncreaseIdentityEvidence)
{
    auto repeated=track(1,0,{1,0},3);
    for(auto& observation:repeated.observations)observation.timestampSeconds=0.;
    const auto candidates=pfcore::characterCandidates({repeated});
    ASSERT_EQ(candidates.size(),1u);
    EXPECT_DOUBLE_EQ(candidates[0].prototype.faceEvidence,1./3.);
    EXPECT_DOUBLE_EQ(candidates[0].prototype.bodyEvidence,1./3.);
    EXPECT_FALSE(candidates[0].distinctIdentity);
}

TEST(CharacterCandidates, OneFaceNeedsSeveralIndependentReferencesAndBodyAgreement)
{
    auto reference=track(1,0,{1,0},4);
    auto brief=track(2,3,{},4);
    brief.observations.front().faceEmbedding={1,0};
    for(auto& observation:reference.observations)observation.appearanceEmbedding={1,0};
    for(auto& observation:brief.observations)observation.appearanceEmbedding={.8F,.6F};
    ASSERT_EQ(pfcore::characterCandidates({reference,brief}).size(),1);

    for(auto& observation:brief.observations)observation.appearanceEmbedding={0,1};
    EXPECT_EQ(pfcore::characterCandidates({reference,brief}).size(),2);

    for(auto& observation:brief.observations)observation.appearanceEmbedding={.8F,.6F};
    reference.observations[2].faceEmbedding.clear();
    reference.observations[3].faceEmbedding.clear();
    EXPECT_EQ(pfcore::characterCandidates({reference,brief}).size(),2);
}

TEST(CharacterCandidates, ShortReferenceSurvivesDilutedWholePersonGallery)
{
    auto reference=track(1,0,{.7F,.7141428F},4);
    auto longHistory=track(2,3,{.3F,.9539392F},400);
    auto brief=track(3,110,{},4);
    for(auto& o:reference.observations)o.appearanceEmbedding={1,0};
    for(auto& o:longHistory.observations)o.appearanceEmbedding={0,1};
    for(auto& o:brief.observations)o.appearanceEmbedding={1,0};
    brief.observations.front().faceEmbedding={1,0};
    const auto joined=pfcore::characterCandidates({reference,longHistory,brief});
    ASSERT_EQ(joined.size(),1);
    EXPECT_EQ(joined.front().trackIds,(std::vector<std::size_t>{1,2,3}));

    // A single lucky face, or mismatching clothing, cannot establish the link.
    reference.observations[1].faceEmbedding.clear();
    reference.observations[2].faceEmbedding.clear();
    EXPECT_EQ(pfcore::characterCandidates({reference,longHistory,brief}).size(),2);
    reference.observations[1].timestampSeconds=reference.observations[0].timestampSeconds;
    reference.observations[1].faceEmbedding={.7F,.7141428F};
    reference.observations[2].timestampSeconds=reference.observations[0].timestampSeconds;
    reference.observations[2].faceEmbedding={.8F,.6F};
    EXPECT_EQ(pfcore::characterCandidates({reference,longHistory,brief}).size(),2);
    reference=track(1,0,{.7F,.7141428F},4);
    for(auto& o:reference.observations)o.appearanceEmbedding={1,0};
    for(auto& o:brief.observations)o.appearanceEmbedding={.8F,.6F};
    EXPECT_EQ(pfcore::characterCandidates({reference,longHistory,brief}).size(),2);
    for(auto& o:longHistory.observations)o.box={0,0,100,200};
    for(std::size_t i=0;i<brief.observations.size();++i) {
        auto& o=brief.observations[i];
        o.appearanceEmbedding={1,0};o.timestampSeconds=10+i*.25;o.box={200,0,300,200};
    }
    EXPECT_EQ(pfcore::characterCandidates({reference,longHistory,brief}).size(),2);
}

TEST(CharacterCandidates, BodyOnlyTrackNeedsAnUnambiguousFaceBackedCandidate)
{
    auto reference=track(1,0,{1,0},4);
    auto other=track(2,10,{0,1},4);
    auto back=track(3,5,{},4);
    for(auto& o:reference.observations)o.appearanceEmbedding={1,0};
    for(auto& o:other.observations)o.appearanceEmbedding={0,1};
    for(auto& o:back.observations)o.appearanceEmbedding={.84F,.5425864F};
    auto candidates=pfcore::characterCandidates({reference,other,back});
    ASSERT_EQ(candidates.size(),2);
    const auto matched=std::find_if(candidates.begin(),candidates.end(),[](const auto& c){return c.id==1;});
    ASSERT_NE(matched,candidates.end());
    EXPECT_EQ(matched->trackIds,(std::vector<std::size_t>{1,3}));

    // Two plausible different face-backed candidates make the body-only
    // observation ambiguous; do not silently choose the closest clothing.
    for(auto& o:other.observations)o.appearanceEmbedding={.5F,.8660254F};
    EXPECT_EQ(pfcore::characterCandidates({reference,other,back}).size(),3);

    for(auto& o:other.observations)o.appearanceEmbedding={0,1};
    for(std::size_t i=0;i<back.observations.size();++i)
        back.observations[i].timestampSeconds=reference.observations[i].timestampSeconds;
    EXPECT_EQ(pfcore::characterCandidates({reference,other,back}).size(),3);
}

TEST(CharacterCandidates, CovisibleSimilarPersonRemainsBodyAmbiguityEvidence)
{
    auto reference=track(1,0,{1,0},4);
    auto competitor=track(2,5,{0,1},4);
    auto back=track(3,5,{},4);
    auto distractor=track(4,10,{0,0,1},4);
    for(auto& o:reference.observations) {o.appearanceEmbedding={1,0};o.box={0,0,100,200};}
    for(auto& o:competitor.observations) {o.appearanceEmbedding={.84F,.5425864F};o.box={200,0,300,200};}
    for(auto& o:back.observations) {o.appearanceEmbedding={.84F,.5425864F};o.box={0,0,100,200};}
    for(auto& o:distractor.observations) {o.appearanceEmbedding={0,1};o.box={0,0,100,200};}
    const auto candidates=pfcore::characterCandidates({reference,competitor,back,distractor});
    EXPECT_EQ(candidates.size(),4);
    for(auto& o:competitor.observations)o.appearanceEmbedding={0,1};
    const auto unambiguous=pfcore::characterCandidates({reference,competitor,back,distractor});
    const auto matched=std::find_if(unambiguous.begin(),unambiguous.end(),[](const auto& c){return c.id==1;});
    ASSERT_NE(matched,unambiguous.end());
    EXPECT_EQ(matched->trackIds,(std::vector<std::size_t>{1,3}));
}

TEST(CharacterCandidates, OccludedDuplicateNeedsHeadTorsoAndBodyAgreement)
{
    auto wide=track(1,0,{},4),partial=track(2,0,{},4);
    for(auto& observation:wide.observations) {
        observation.box={0,0,866,705};observation.appearanceEmbedding={1,0};
        observation.keypoints.resize(17);
        observation.keypoints[4]={100,100,.95};observation.keypoints[6]={200,300,.95};
    }
    for(auto& observation:partial.observations) {
        observation.box={0,0,572,705};observation.appearanceEmbedding={.87F,.4930517F};
        observation.keypoints.resize(17);
        observation.keypoints[4]={106,106,.95};observation.keypoints[6]={241,318,.95};
    }
    ASSERT_EQ(pfcore::characterCandidates({wide,partial}).size(),1);
    // A crossing or otherwise different pose is not a duplicate even when
    // boxes and appearance overlap closely.
    partial.observations[2].keypoints[6].x+=180;
    EXPECT_EQ(pfcore::characterCandidates({wide,partial}).size(),2);
    partial.observations[2].keypoints[6].x-=180;
    for(auto& observation:partial.observations)observation.appearanceEmbedding={0,1};
    EXPECT_EQ(pfcore::characterCandidates({wide,partial}).size(),2);
}

TEST(CharacterCandidates, DuplicateHeadCanJoinAnExistingMultiTrackIdentity)
{
    auto anchor=track(1,0,{1,0},4), earlier=track(2,3,{1,0},4), duplicate=track(3,0,{},4);
    for (auto* person : {&anchor,&earlier,&duplicate}) for (auto& observation : person->observations) {
        observation.box={0,0,100,200}; observation.appearanceEmbedding={1,0};
        observation.keypoints.resize(17);
        for (int i=0;i<5;++i) observation.keypoints[i]={30.+i*5,30.+i*3,.95};
    }
    auto result=pfcore::characterCandidates({anchor,earlier,duplicate});
    ASSERT_EQ(result.size(),1U);
    EXPECT_EQ(result.front().trackIds,(std::vector<std::size_t>{1,2,3}));
    auto hidden=duplicate;
    for (auto& observation:hidden.observations)
        for (int i=1;i<5;++i) observation.keypoints[i].confidence=0;
    EXPECT_EQ(pfcore::characterCandidates({anchor,earlier,hidden}).size(),2U);
    auto nonfinite=duplicate;
    for (auto& observation:nonfinite.observations)
        for (int i=1;i<5;++i) observation.keypoints[i].confidence=std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ(pfcore::characterCandidates({anchor,earlier,nonfinite}).size(),2U);
    auto brief=duplicate;
    brief.observations.resize(2);
    EXPECT_EQ(pfcore::characterCandidates({anchor,earlier,brief}).size(),2U);
    auto conflictingFace=duplicate;
    for (auto& observation:conflictingFace.observations) observation.faceEmbedding={0,1};
    EXPECT_EQ(pfcore::characterCandidates({anchor,earlier,conflictingFace}).size(),2U);
    for (auto& observation:duplicate.observations)
        for (int i=0;i<5;++i) observation.keypoints[i].x+=30;
    result=pfcore::characterCandidates({anchor,earlier,duplicate});
    ASSERT_EQ(result.size(),2U);
    EXPECT_EQ(result.front().trackIds,(std::vector<std::size_t>{1,2}));
}

TEST(CharacterCandidates, ContinuityAcrossCutNeedsTheSameObservedCompanion)
{
    auto companion=track(1,0,{1,0},8);
    auto before=track(2,0,{},4),after=track(3,1,{},4);
    for(auto& o:companion.observations){o.box={0,0,100,200};o.appearanceEmbedding={0,1};}
    for(auto& o:before.observations){o.box={200,0,300,200};o.appearanceEmbedding={1,0};}
    for(auto& o:after.observations){o.box={201,0,301,200};o.appearanceEmbedding={.65F,.7599342F};}
    auto result=pfcore::characterCandidates({companion,before,after});
    ASSERT_EQ(result.size(),2);
    const auto recovered=std::find_if(result.begin(),result.end(),[](const auto& c){return c.id==2;});
    ASSERT_NE(recovered,result.end());
    EXPECT_EQ(recovered->trackIds,(std::vector<std::size_t>{2,3}));

    // One side lacks the same companion, so the cut has no identity context.
    companion.observations.erase(companion.observations.begin()+4);
    EXPECT_EQ(pfcore::characterCandidates({companion,before,after}).size(),3);
    companion=track(1,0,{1,0},8);
    for(auto& o:companion.observations){o.box={0,0,100,200};o.appearanceEmbedding={0,1};}
    for(auto& o:before.observations)o.faceEmbedding={1,0};
    for(auto& o:after.observations)o.faceEmbedding={0,1};
    EXPECT_EQ(pfcore::characterCandidates({companion,before,after}).size(),3);
}

TEST(CharacterCandidates, FaceBackedCutContinuityRequiresIndependentEvidence)
{
    auto before=track(1,0,{1,0},4),after=track(2,1,{},4);
    for(auto& o:before.observations){o.box={0,0,100,200};o.appearanceEmbedding={1,0};}
    for(auto& o:after.observations){o.box={3,0,103,200};o.appearanceEmbedding={.73F,.6834471F};}
    after.observations.front().faceEmbedding={.53F,.848F};
    const auto matched=pfcore::characterCandidates({before,after});
    ASSERT_EQ(matched.size(),1);
    EXPECT_EQ(matched.front().trackIds,(std::vector<std::size_t>{1,2}));
    auto competing=after; competing.id=3;
    EXPECT_EQ(pfcore::characterCandidates({before,after,competing}).size(),3);

    // Position alone, weak facial agreement, unrelated clothing, or a
    // displaced box must not identify a person across a cut.
    after.observations.front().faceEmbedding.clear();
    EXPECT_EQ(pfcore::characterCandidates({before,after}).size(),2);
    after.observations.front().faceEmbedding={.4F,.916515F};
    EXPECT_EQ(pfcore::characterCandidates({before,after}).size(),2);
    after.observations.front().faceEmbedding={.53F,.848F};
    for(auto& o:after.observations)o.appearanceEmbedding={0,1};
    EXPECT_EQ(pfcore::characterCandidates({before,after}).size(),2);
    for(auto& o:after.observations){o.appearanceEmbedding={.73F,.6834471F};o.box={80,0,180,200};}
    EXPECT_EQ(pfcore::characterCandidates({before,after}).size(),2);
}

TEST(CharacterCandidates, MultiViewRecoveryCannotMergeConcurrentPeopleThroughAThirdView)
{
    auto front=track(1,0,{.7660444F,.6427876F},6);
    for(std::size_t i=3;i<front.observations.size();++i)
        front.observations[i].faceEmbedding={.7660444F,-.6427876F};
    auto profile=track(2,3,{.3420201F,.9396926F});
    auto bystander=track(3,0,{.3420201F,.9396926F});
    for(auto* person:{&front,&profile,&bystander})for(auto& o:person->observations)o.appearanceEmbedding={1,0};
    const auto candidates=pfcore::characterCandidates({front,profile,bystander});
    ASSERT_EQ(candidates.size(),2);
    for(const auto& c:candidates)
        EXPECT_FALSE(std::find(c.trackIds.begin(),c.trackIds.end(),1)!=c.trackIds.end()
            && std::find(c.trackIds.begin(),c.trackIds.end(),3)!=c.trackIds.end());
}

TEST(CharacterCandidates, DuplicateDetectionDoesNotBlockRepeatedPersonButDistinctSkeletonDoes)
{
    auto first=track(1,0,{.7660444F,.6427876F},6);
    for(std::size_t i=3;i<first.observations.size();++i)
        first.observations[i].faceEmbedding={.7660444F,-.6427876F};
    auto second=track(2,1.25,{.3420201F,.9396926F});
    for(auto* person:{&first,&second})for(auto& observation:person->observations) {
        observation.appearanceEmbedding={1,0};observation.box={0,0,100,200};
        observation.keypoints.resize(17);
        for(auto j:{0,1,2,5,6,9,10})
            observation.keypoints[j]={20.+j*2,30.+j*3,.9};
    }
    const auto duplicate=pfcore::characterCandidates({first,second});
    ASSERT_EQ(duplicate.size(),1);
    EXPECT_EQ(duplicate.front().trackIds,(std::vector<std::size_t>{1,2}));

    for(auto& point:second.observations.front().keypoints)if(point.confidence>0)point.x+=30;
    const auto different=pfcore::characterCandidates({first,second});
    ASSERT_EQ(different.size(),2);
}

TEST(CharacterCandidates, MultiViewBridgeCannotJoinFacesWithRepeatedContradictoryEvidence)
{
    // A third view contains plausible samples for each person. Its average is
    // intentionally distant from both, so this exercises recovery rather than
    // the initial mean-based grouping. A-B lack any supporting face pair.
    auto a=track(1,0,{1,0,0},4);
    auto b=track(2,4,{0,1,0},4);
    auto bridge=track(3,8,{0,0,1},18);
    for(int i=0;i<3;++i)bridge.observations[i].faceEmbedding={1,0,0};
    for(int i=3;i<6;++i)bridge.observations[i].faceEmbedding={0,1,0};
    for(auto* person:{&a,&b,&bridge})
        for(auto& observation:person->observations)observation.appearanceEmbedding={1,0,0};
    const auto candidates=pfcore::characterCandidates({a,b,bridge});
    for(const auto& candidate:candidates) {
        const auto hasA=std::find(candidate.trackIds.begin(),candidate.trackIds.end(),1)!=candidate.trackIds.end();
        const auto hasB=std::find(candidate.trackIds.begin(),candidate.trackIds.end(),2)!=candidate.trackIds.end();
        EXPECT_FALSE(hasA && hasB);
    }
}

TEST(CharacterCandidates, BatchSuggestionRequiresStrongUniqueFaceEvidence)
{
    const auto target = pfcore::characterCandidates({track(1,0,{1,0})}).front();
    auto candidates = pfcore::characterCandidates({track(2,0,{1,0}),track(3,2,{0,1})});
    auto suggestion = pfcore::suggestTargetCharacter(target,candidates);
    EXPECT_TRUE(suggestion.automatic);
    EXPECT_EQ(suggestion.candidateId,2);
    candidates.push_back(candidates.front()); candidates.back().id=4;
    suggestion = pfcore::suggestTargetCharacter(target,candidates);
    EXPECT_FALSE(suggestion.automatic);
    EXPECT_EQ(suggestion.reason,pfcore::TargetSuggestionReason::Ambiguous);
    auto bodyOnly = target; bodyOnly.prototype.face.clear();
    EXPECT_FALSE(pfcore::suggestTargetCharacter(bodyOnly,candidates).automatic);
}

TEST(CharacterCandidates, CancelDoesNotPublishPartialCandidates)
{
    pfcore::DominantSelectionControl control;
    control.cancelled = [] { return true; };
    EXPECT_TRUE(pfcore::characterCandidates({track(1,0,{1,0})},control).empty());
}

TEST(CharacterCandidates, SerialAndParallelPreserveComponentsAndLegacyWinner)
{
    std::vector<pfcore::PersonTrack> tracks;
    for (std::size_t i=0;i<260;++i) tracks.push_back(track(i+1,i*2,i%2 ? std::vector<float>{0,1} : std::vector<float>{1,0}));
    pfcore::DominantSelectionControl serial, parallel;
    serial.maxThreads=1; parallel.maxThreads=4;
    const auto a=pfcore::characterCandidates(tracks,serial), b=pfcore::characterCandidates(tracks,parallel);
    ASSERT_EQ(a.size(),2); ASSERT_EQ(b.size(),a.size());
    for (std::size_t i=0;i<a.size();++i) EXPECT_EQ(a[i].trackIds,b[i].trackIds);
}
