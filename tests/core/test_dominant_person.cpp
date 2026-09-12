#include <gtest/gtest.h>

#include <pfcore/DominantPerson.hpp>

namespace {

pfcore::PersonDetection person(double timestamp, double left, double confidence)
{
    pfcore::PersonDetection result;
    result.timestampSeconds = timestamp;
    result.frameDurationSeconds = 0.1;
    result.box = {left, 0.0, left + 10.0, 20.0};
    result.confidence = confidence;
    result.keypointConfidence = confidence;
    result.keypoints = {{left, 0.0, confidence}};
    return result;
}

TEST(DominantPerson, IoUAssociatesTheSamePersonAcrossFrames)
{
    pfcore::DominantPersonTracker tracker;
    tracker.update(0.0, 0.1, {person(0.0, 0.0, 0.8)});
    tracker.update(0.1, 0.1, {person(0.1, 1.0, 0.8)});
    ASSERT_EQ(tracker.tracks().size(), 1U);
    ASSERT_EQ(tracker.tracks().front().observations.size(), 2U);
}

TEST(DominantPerson, ChoosesLongestTrackThenArea)
{
    pfcore::DominantPersonTracker tracker;
    tracker.update(0.0, 0.1, {person(0.0, 0.0, 0.8), person(0.0, 40.0, 0.9)});
    tracker.update(0.1, 0.1, {person(0.1, 1.0, 0.8), person(0.1, 41.0, 0.9)});
    tracker.update(0.2, 0.1, {person(0.2, 2.0, 0.8)});
    const auto dominant = tracker.dominant();
    ASSERT_TRUE(dominant.has_value());
    EXPECT_EQ(dominant->observations.size(), 3U);
}

} // namespace
