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

TEST(DominantPerson, DoesNotBridgeLongDetectionGaps)
{
    pfcore::DominantPersonTracker tracker(0.30, 0.5);
    tracker.update(0.0, 0.1, {person(0.0, 0.0, 0.8)});
    tracker.update(2.0, 0.1, {person(2.0, 0.1, 0.8)});
    ASSERT_EQ(tracker.tracks().size(), 2U);
    EXPECT_EQ(tracker.tracks().front().observations.size(), 1U);
    EXPECT_EQ(tracker.tracks().back().observations.size(), 1U);
}

TEST(DominantPerson, ShotLocalSelectionDoesNotDiscardASeparateOutfitOrProfile)
{
    std::vector<pfcore::PersonTrack> tracks(2);
    for (int i = 0; i < 12; ++i) {
        auto observation = person(i * 0.1, 0, 0.9);
        observation.faceEmbedding = {1, 0};
        tracks[0].observations.push_back(observation);
    }
    for (int i = 0; i < 4; ++i) {
        auto observation = person(2.0 + i * 0.1, 0, 0.9);
        observation.faceEmbedding = {0, 1};
        tracks[1].observations.push_back(observation);
    }
    EXPECT_EQ(pfcore::selectDominantSceneTracks(tracks, 0, 1.5),
              (std::vector<bool>{true, false}));
    EXPECT_EQ(pfcore::selectDominantSceneTracks(tracks, 2, 2.5),
              (std::vector<bool>{false, true}));
}

TEST(DominantPerson, ShotLocalSelectionIgnoresDurationAndIdentityOutsideTheShot)
{
    std::vector<pfcore::PersonTrack> tracks(2);
    for (int i = 0; i < 30; ++i) {
        auto observation = person(i * 0.1, 0, 0.9);
        observation.faceEmbedding = i < 20 ? std::vector<float>{1, 0}
                                           : std::vector<float>{0, 1};
        tracks[0].observations.push_back(observation);
    }
    for (int i = 0; i < 8; ++i) {
        auto observation = person(i * 0.1, 40, 0.9);
        observation.faceEmbedding = {0, 1};
        tracks[1].observations.push_back(observation);
    }
    // A longer whole-file track is only briefly visible in this shot and
    // cannot absorb the other person through later, out-of-shot evidence.
    EXPECT_EQ(pfcore::selectDominantSceneTracks(tracks, 0, 0.8),
              (std::vector<bool>{true, false}));
    tracks[0].observations.erase(tracks[0].observations.begin() + 2,
                                tracks[0].observations.begin() + 8);
    EXPECT_EQ(pfcore::selectDominantSceneTracks(tracks, 0, 0.8),
              (std::vector<bool>{false, true}));
}

TEST(DominantPerson, ShotSelectionKeepsSameIdentityFragmentsButNotBackgroundPerson)
{
    std::vector<pfcore::PersonTrack> tracks(3);
    for (std::size_t track = 0; track < tracks.size(); ++track) {
        for (int i = 0; i < 3; ++i) {
            auto observation = person(i * 0.1 + (track == 1 ? 0.4 : 0), track * 40, 0.9);
            observation.faceEmbedding = track < 2 ? std::vector<float>{1, 0}
                                                 : std::vector<float>{0, 1};
            tracks[track].observations.push_back(observation);
        }
    }
    EXPECT_EQ(pfcore::selectDominantSceneTracks(tracks, 0, 1),
              (std::vector<bool>{true, true, false}));
    EXPECT_EQ(pfcore::selectDominantSceneTracks(tracks, 1, 1),
              (std::vector<bool>{false, false, false}));
}

TEST(DominantPerson, VerifiedLeadIsNotReplacedByLongerForegroundInterlocutor)
{
    std::vector<pfcore::PersonTrack> tracks(2);
    for (std::size_t track = 0; track < tracks.size(); ++track) {
        for (int i = 0; i < (track == 0 ? 3 : 6); ++i) {
            auto observation = person(i * 0.1, track * 40, 0.9);
            observation.faceEmbedding = track == 0 ? std::vector<float>{1, 0}
                                                  : std::vector<float>{0, 1};
            tracks[track].observations.push_back(observation);
        }
    }
    EXPECT_EQ(pfcore::selectDominantSceneTracks(tracks, 0, 0.6, {true, false}),
              (std::vector<bool>{true, false}));
    // Recover the local identity only after the preferred lead leaves.
    EXPECT_EQ(pfcore::selectDominantSceneTracks(tracks, 0.3, 0.6, {true, false}),
              (std::vector<bool>{false, true}));
}

} // namespace
