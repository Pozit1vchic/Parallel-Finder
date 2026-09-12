#include <gtest/gtest.h>

#include <pfcore/MovementClassifier.hpp>
#include <pfcore/MotionRanker.hpp>

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

} // namespace
