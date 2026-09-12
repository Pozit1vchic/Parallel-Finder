#include <gtest/gtest.h>
#include <pfcore/MotionMatcher.hpp>

#include <set>

namespace {

pfcore::MotionWindow window(const char* source, double offset, bool mirrored = false)
{
    pfcore::MotionWindow result; result.sourceId = source;
    for (int i = 0; i < 6; ++i) {
        const double x = static_cast<double>(i) / 5.0;
        result.frames.push_back({offset + i * 0.1, {{0, 0}, {mirrored ? -x : x, x}}});
    }
    return result;
}

TEST(MotionMatcher, IdenticalNormalizedMotionScoresHighly)
{
    pfcore::MotionMatcher matcher;
    const auto match = matcher.compare(window("a", 0), window("b", 4));
    EXPECT_GT(match.similarity, 0.8);
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
