#include <gtest/gtest.h>

#include <pfcore/MovementClassifier.hpp>
#include <pfcore/MotionRanker.hpp>
#include <algorithm>
#include <array>
#include <limits>

namespace {

pfcore::MotionWindow moving(const char* source, double dx, double dy)
{
    pfcore::MotionWindow window;
    window.sourceId = source;
    for (int frame = 0; frame < 6; ++frame) {
        const double t = static_cast<double>(frame) / 5.0;
        window.frames.push_back({t, {{0.0 + dx * t, 0.0 + dy * t},
                                     {10.0 + dx * t, 0.0 + dy * t},
                                     {5.0 + dx * t, 10.0 + dy * t},
                                     {3.0 + dx * t, 12.0 + dy * t},
                                     {7.0 + dx * t, 12.0 + dy * t},
                                     {2.0 + dx * t, 4.0 + dy * t},
                                     {8.0 + dx * t, 4.0 + dy * t},
                                     {1.0 + dx * t, 14.0 + dy * t},
                                     {9.0 + dx * t, 14.0 + dy * t},
                                     {0.0 + dx * t, 10.0 + dy * t},
                                     {10.0 + dx * t, 10.0 + dy * t}}});
    }
    return window;
}

TEST(MovementClassifier, LabelsHorizontalMotionDeterministically)
{
    const auto result = pfcore::MovementClassifier().classify(moving("a", 20.0, 0.0));
    EXPECT_EQ(result.direction, pfcore::MovementDirection::Right);
    EXPECT_STREQ(pfcore::MovementClassifier::directionName(result.direction), "right");
}

TEST(MovementClassifier, LabelsStaticMotion)
{
    const auto result = pfcore::MovementClassifier().classify(moving("a", 0.0, 0.0));
    EXPECT_EQ(result.direction, pfcore::MovementDirection::Static);
    EXPECT_EQ(result.gesture, pfcore::GestureClass::Static);
}

TEST(MotionRanker, AddsLabelsAndStableScore)
{
    const auto left = moving("a", 20.0, 0.0);
    const auto right = moving("b", 20.0, 0.0);
    pfcore::MotionMatch match;
    match.leftIndex = 0; match.rightIndex = 1; match.similarity = 0.9; match.durationSeconds = 1.0;
    std::vector<pfcore::MotionMatch> matches {match};
    pfcore::MotionRanker::rank(matches, {left, right});
    ASSERT_EQ(matches.size(), 1U);
    EXPECT_EQ(matches.front().directionLabel, "right");
    EXPECT_GT(matches.front().rankScore, 0.0);
}

TEST(MotionRanker, RetainsClosedHandGestureButRejectsJitter)
{
    auto left = moving("a", 0.0, 0.0);
    // Raise and lower both hands: the endpoint pose is unchanged.
    const double lift[] = {0.0, 2.0, 4.0, 4.0, 2.0, 0.0};
    for (std::size_t i = 0; i < left.frames.size(); ++i) {
        left.frames[i].keypoints[9].y -= lift[i];
        left.frames[i].keypoints[10].y -= lift[i];
    }
    auto right = left;
    right.sourceId = "b";
    pfcore::MotionMatch match;
    match.leftIndex = 0; match.rightIndex = 1;
    match.similarity = 0.9; match.durationSeconds = 1.0;
    std::vector<pfcore::MotionMatch> matches{match};
    pfcore::MotionRanker::rank(matches, {left, right});
    EXPECT_EQ(matches.size(), 1u);

    for (std::size_t i = 0; i < left.frames.size(); ++i) {
        left.frames[i].keypoints[9].y = 10.0 - lift[i] * 0.005;
        left.frames[i].keypoints[10].y = 10.0 - lift[i] * 0.005;
    }
    matches = {match};
    pfcore::MotionRanker::rank(matches, {left, left});
    EXPECT_TRUE(matches.empty());

    left = moving("a", 0.0, 0.0);
    left.frames[2].keypoints[9].y -= 4.0;
    left.frames[2].keypoints[10].y -= 4.0;
    matches = {match};
    pfcore::MotionRanker::rank(matches, {left, left});
    EXPECT_TRUE(matches.empty()) << "A single detection outlier is not a closed gesture";
}

TEST(MotionRanker, NearEqualScoresAreOrderedConsistentlyAcrossAllPermutations)
{
    std::vector<pfcore::MotionWindow> windows(4);
    for (auto& window : windows) window.staticFrameSet = true;
    std::array<std::size_t, 3> order{0, 1, 2};
    const std::array<double, 3> scores{.9, .9 + .75e-12, .9 + 1.5e-12};
    do {
        std::vector<pfcore::MotionMatch> matches;
        for (const auto index : order) {
            pfcore::MotionMatch match;
            match.leftIndex = index;
            match.rightIndex = 3;
            match.similarity = scores[index];
            matches.push_back(match);
        }
        pfcore::MotionRanker::rank(matches, windows);
        ASSERT_EQ(matches.size(), 3);
        EXPECT_EQ(matches[0].leftIndex, 2);
        EXPECT_EQ(matches[1].leftIndex, 1);
        EXPECT_EQ(matches[2].leftIndex, 0);
    } while (std::next_permutation(order.begin(), order.end()));
}

TEST(MotionRanker, NaNScoresHaveAConsistentPositionAfterNumericScores)
{
    std::vector<pfcore::MotionWindow> windows(5);
    for (auto& window : windows) window.staticFrameSet = true;
    std::array<std::size_t, 4> order{0, 1, 2, 3};
    const std::array<double, 4> scores{std::numeric_limits<double>::quiet_NaN(), .8, .9,
        std::numeric_limits<double>::quiet_NaN()};
    do {
        std::vector<pfcore::MotionMatch> matches;
        for (const auto index : order) {
            pfcore::MotionMatch match;
            match.leftIndex = index;
            match.rightIndex = 4;
            match.similarity = scores[index];
            matches.push_back(match);
        }
        pfcore::MotionRanker::rank(matches, windows);
        ASSERT_EQ(matches.size(), 4);
        EXPECT_EQ(matches[0].leftIndex, 2);
        EXPECT_EQ(matches[1].leftIndex, 1);
        EXPECT_EQ(matches[2].leftIndex, 0);
        EXPECT_EQ(matches[3].leftIndex, 3);
    } while (std::next_permutation(order.begin(), order.end()));
}

} // namespace
