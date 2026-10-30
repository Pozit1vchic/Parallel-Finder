#include <pfservices/CharacterAnalysisCache.hpp>
#include <gtest/gtest.h>

namespace {
pfservices::CharacterAnalysis artifact()
{
    pfservices::CharacterAnalysis a;
    pfcore::CharacterCandidate c;
    c.id=3; c.trackIds={3}; c.observationCount=5;
    c.presenceSeconds=1; c.evidenceQuality=.8;
    c.prototype.face={1,0}; c.prototype.faceEvidence=1;
    c.appearances={{3,.25,.8},{3,.75,.8}};
    a.candidates.push_back(c);
    pfcore::MotionWindow w;
    w.sourceId="source"; w.trackId=3;
    w.frames={pfcore::PoseFrame{0,{{1,2,1}}},pfcore::PoseFrame{1,{{2,3,1}}}};
    a.windows.push_back(w); a.sceneCount=2;
    return a;
}
}

TEST(CharacterAnalysisCache, RetainsAllCandidatesWindowsAndInspectionEvidence)
{
    const auto a=artifact(); pfservices::CharacterAnalysis loaded;
    ASSERT_TRUE(pfservices::deserializeCharacterAnalysis(pfservices::serializeCharacterAnalysis(a),loaded));
    ASSERT_EQ(loaded.candidates.size(),1); ASSERT_EQ(loaded.windows.size(),1);
    EXPECT_EQ(loaded.sceneCount,2); EXPECT_EQ(loaded.candidates[0].id,3);
    EXPECT_EQ(loaded.candidates[0].appearances.size(),2);
    EXPECT_EQ(loaded.candidates[0].prototype.face,a.candidates[0].prototype.face);
    EXPECT_EQ(loaded.windows[0].frames.size(),2);
}

TEST(CharacterAnalysisCache, TruncationNeverPublishesPartialArtifact)
{
    const auto bytes=pfservices::serializeCharacterAnalysis(artifact());
    for (std::size_t n=0;n<bytes.size();++n) {
        auto output=artifact(); output.sceneCount=9;
        EXPECT_FALSE(pfservices::deserializeCharacterAnalysis(std::span(bytes).first(n),output)) << n;
        EXPECT_EQ(output.sceneCount,9);
    }
}

TEST(CharacterAnalysisCache, RejectsForeignWindowsDuplicateTracksAndIncompatibleVersion)
{
    auto a=artifact(); pfservices::CharacterAnalysis output;
    a.windows[0].trackId=7;
    EXPECT_FALSE(pfservices::deserializeCharacterAnalysis(pfservices::serializeCharacterAnalysis(a),output));
    a=artifact(); a.candidates.push_back(a.candidates.front()); a.candidates.back().id=7;
    EXPECT_FALSE(pfservices::deserializeCharacterAnalysis(pfservices::serializeCharacterAnalysis(a),output));
    auto bytes=pfservices::serializeCharacterAnalysis(artifact()); bytes[4]=99;
    EXPECT_FALSE(pfservices::deserializeCharacterAnalysis(bytes,output));
}
