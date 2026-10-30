#include <pfcore/DominantPerson.hpp>
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
