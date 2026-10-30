#include <pfcore/DominantPerson.hpp>
#include <pfcore/FaceAssociation.hpp>
#include <gtest/gtest.h>

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

TEST(CharacterCandidates, AmbiguousDisconnectedAppearanceIsNotAnotherUniquePerson)
{
    const auto candidates=pfcore::characterCandidates({track(1,0,{1,0}),track(2,2,{.25F,.9682458F})});
    ASSERT_EQ(candidates.size(),2);
    EXPECT_TRUE(candidates[0].distinctIdentity);
    EXPECT_FALSE(candidates[1].distinctIdentity);
    EXPECT_EQ(candidates[1].trackIds,(std::vector<std::size_t>{2}));
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
    people={actual,actual};pfcore::enforceUniqueFaceOwnership(people,std::vector{face,other});
    EXPECT_FALSE(people[0].faceEmbedding.empty());EXPECT_FALSE(people[1].faceEmbedding.empty());
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
