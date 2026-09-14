#include <gtest/gtest.h>
#include <pfcore/MotionMatcher.hpp>

#include <algorithm>
#include <set>

namespace {

pfcore::MotionWindow window(const char* source, double offset, bool mirrored = false,
                            int frameCount = 24, double frameStep = 1.0 / 24.0)
{
    pfcore::MotionWindow result; result.sourceId = source;
    for (int i = 0; i < frameCount; ++i) {
        const double x = static_cast<double>(i) / std::max(1, frameCount - 1);
        result.frames.push_back({offset + i * frameStep,
                                 {{0, 0}, {mirrored ? -x : x, x}}});
    }
    return result;
}

TEST(MotionMatcher, IdenticalNormalizedMotionScoresHighly)
{
    pfcore::MotionMatcher matcher;
    const auto match = matcher.compare(window("a", 0), window("b", 4));
    EXPECT_GT(match.similarity, 0.8);
    EXPECT_LT(match.similarity, 0.995);
}

TEST(MotionMatcher, AllPairsAllowsOneWindowInSeveralResults)
{
    pfcore::MotionMatcherParams params; params.similarityThreshold = 0.7; params.maxUniqueResults = 10;
    pfcore::MotionMatcher matcher(params);
    const auto matches = matcher.findAllPairs({window("a", 0), window("b", 4), window("c", 8)});
    EXPECT_EQ(matches.size(), 3U);
}

TEST(MotionMatcher, BadParametersAreRejected)
{
    pfcore::MotionMatcher matcher;
    auto params = matcher.params(); params.dtwBand = 0;
    EXPECT_THROW(matcher.setParams(params), std::invalid_argument);
}

TEST(MotionMatcher, CrossFileGapIsApplied)
{
    pfcore::MotionMatcherParams params;
    params.similarityThreshold = 0.1;
    params.candidateThreshold = 0.0;
    params.crossFileGapSec = 10.0;
    params.maxUniqueResults = 10;
    pfcore::MotionMatcher matcher(params);
    EXPECT_TRUE(matcher.findAllPairs({window("a", 0.0), window("b", 4.0)}).empty());
    EXPECT_FALSE(matcher.findAllPairs({window("a", 0.0), window("b", 12.0)}).empty());
}

TEST(MotionMatcher, RejectsInvalidTemporalParameters)
{
    pfcore::MotionMatcher matcher;
    auto params = matcher.params();
    params.sakoeChibaRatio = 1.1;
    EXPECT_THROW(matcher.setParams(params), std::invalid_argument);
}

TEST(MotionMatcher, RejectsStaticWindows)
{
    pfcore::MotionWindow left; left.sourceId = "left";
    pfcore::MotionWindow right; right.sourceId = "right";
    for (int i = 0; i < 24; ++i) {
        left.frames.push_back({i / 24.0, {{0.2, 0.2}, {0.2, 0.4}}});
        right.frames.push_back({4.0 + i / 24.0, {{0.2, 0.2}, {0.2, 0.4}}});
    }
    EXPECT_DOUBLE_EQ(pfcore::MotionMatcher().compare(left, right).similarity, 0.0);
    EXPECT_TRUE(pfcore::MotionMatcher().findAllPairs({left, right}).empty());
}

TEST(MotionMatcher, RejectsDetectorJitterAndSameSceneRepeats)
{
    pfcore::MotionWindow left; left.sourceId = "clip-a"; left.hasSceneIndex = true; left.sceneIndex = 3;
    pfcore::MotionWindow right; right.sourceId = "clip-b"; right.hasSceneIndex = true; right.sceneIndex = 4;
    for (int i = 0; i < 24; ++i) {
        const double jitter = (i % 3 == 0 ? 0.004 : -0.003);
        left.frames.push_back({i / 24.0, {{0.2 + jitter, 0.2}, {0.2, 0.4 + jitter}}});
        right.frames.push_back({6.0 + i / 24.0, {{0.2 - jitter, 0.2}, {0.2, 0.4 - jitter}}});
    }
    EXPECT_TRUE(pfcore::MotionMatcher().findAllPairs({left, right}).empty());
}

TEST(MotionMatcher, RejectsSameSceneEvenWhenThePoseMoves)
{
    auto left = window("same", 0.0);
    auto right = window("same", 8.0);
    left.hasSceneIndex = right.hasSceneIndex = true;
    left.sceneIndex = right.sceneIndex = 2;
    EXPECT_DOUBLE_EQ(pfcore::MotionMatcher().compare(left, right).similarity, 0.0);
}

TEST(MotionMatcher, RequiresAContinuousTemporalRun)
{
    auto left = window("left", 0.0, false, 24);
    auto right = window("right", 4.0, false, 24);
    // Keep the endpoints similar but break the middle of the trajectory. A
    // pair of isolated high-similarity frames must not become a result.
    for (int i = 6; i < 18; ++i)
        right.frames[static_cast<std::size_t>(i)].keypoints.push_back({0.3, 0.8});
    EXPECT_DOUBLE_EQ(pfcore::MotionMatcher().compare(left, right).similarity, 0.0);
}

TEST(MotionMatcher, EnforcesHardSameSourceGap)
{
    pfcore::MotionMatcherParams params;
    params.candidateThreshold = 0.0;
    params.similarityThreshold = 0.1;
    params.minRepeatGapSec = 0.0;
    params.sameFileGapSec = 0.0;
    params.maxUniqueResults = 10;
    auto left = window("same", 0.0);
    auto near = window("same", 3.0);
    auto far = window("same", 6.0);
    EXPECT_TRUE(pfcore::MotionMatcher(params).findAllPairs({left, near}).empty());
    EXPECT_FALSE(pfcore::MotionMatcher(params).findAllPairs({left, far}).empty());
}

TEST(MotionMatcher, RejectsDifferentTracksWithinOneSource)
{
    pfcore::MotionMatcherParams params;
    params.candidateThreshold = 0.0;
    params.similarityThreshold = 0.1;
    params.minRepeatGapSec = 0.0;
    params.sameFileGapSec = 0.0;
    params.maxUniqueResults = 10;
    auto left = window("same", 0.0);
    auto right = window("same", 8.0);
    left.trackId = 11;
    right.trackId = 12;
    left.hasSceneIndex = right.hasSceneIndex = true;
    left.sceneIndex = 1;
    right.sceneIndex = 2;
    EXPECT_TRUE(pfcore::MotionMatcher(params).findAllPairs({left, right}).empty());
    EXPECT_DOUBLE_EQ(pfcore::MotionMatcher(params).compare(left, right).similarity, 0.0);
}

TEST(MotionMatcher, AppearanceGateRejectsDifferentPeople)
{
    pfcore::MotionMatcherParams params;
    params.candidateThreshold = 0.0;
    params.similarityThreshold = 0.1;
    params.crossFileGapSec = 0.0;
    params.maxUniqueResults = 10;
    params.requireAppearance = true;
    params.minAppearanceSimilarity = 0.80;
    auto left = window("left", 0.0);
    auto right = window("right", 4.0);
    left.appearanceEmbedding = {1.0F, 0.0F, 0.0F};
    right.appearanceEmbedding = {0.0F, 1.0F, 0.0F};
    EXPECT_TRUE(pfcore::MotionMatcher(params).findAllPairs({left, right}).empty());
}

TEST(MotionMatcher, AppearanceGateAcceptsSamePerson)
{
    pfcore::MotionMatcherParams params;
    params.candidateThreshold = 0.0;
    params.similarityThreshold = 0.1;
    params.crossFileGapSec = 0.0;
    params.maxUniqueResults = 10;
    params.requireAppearance = true;
    params.minAppearanceSimilarity = 0.80;
    auto left = window("left", 0.0);
    auto right = window("right", 4.0);
    left.appearanceEmbedding = {1.0F, 0.0F, 0.0F};
    right.appearanceEmbedding = {0.98F, 0.12F, 0.0F};
    EXPECT_FALSE(pfcore::MotionMatcher(params).findAllPairs({left, right}).empty());
}

TEST(MotionMatcher, SyntheticAcceptanceF1RemainsAboveThreshold)
{
    pfcore::MotionMatcherParams params;
    params.similarityThreshold = 0.70;
    params.candidateThreshold = 0.40;
    params.maxUniqueResults = 20;
    pfcore::MotionWindow unrelated;
    unrelated.sourceId = "d";
    for (int i = 0; i < 6; ++i) {
        const double x = static_cast<double>(i) / 5.0;
        unrelated.frames.push_back({12.0 + i * 0.1,
                                    {{0, 0}, {x, x}, {x * x, -x}}});
    }
    const std::vector<pfcore::MotionWindow> windows = {
        window("a", 0.0), window("b", 4.0), window("c", 8.0), unrelated};
    const auto matches = pfcore::MotionMatcher(params).findAllPairs(windows);
    const std::set<std::pair<std::size_t, std::size_t>> expected = {{0, 1}, {0, 2}, {1, 2}};
    std::set<std::pair<std::size_t, std::size_t>> actual;
    for (const auto& match : matches) actual.emplace(match.leftIndex, match.rightIndex);
    std::size_t truePositives = 0;
    for (const auto& pair : actual) if (expected.contains(pair)) ++truePositives;
    const std::size_t falsePositives = actual.size() - truePositives;
    const std::size_t falseNegatives = expected.size() - truePositives;
    const double precision = truePositives == 0 ? 0.0
        : static_cast<double>(truePositives) / static_cast<double>(truePositives + falsePositives);
    const double recall = static_cast<double>(truePositives)
        / static_cast<double>(truePositives + falseNegatives);
    const double f1 = precision + recall == 0.0 ? 0.0
        : 2.0 * precision * recall / (precision + recall);
    EXPECT_GE(f1, 0.90);
}

} // namespace
