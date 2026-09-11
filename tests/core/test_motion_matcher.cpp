#include <gtest/gtest.h>
#include <pfcore/MotionMatcher.hpp>

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

} // namespace
