#include <gtest/gtest.h>

#include <pfcore/DominantPerson.hpp>
#include <cmath>

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
    // The interlocutor does not become the source's lead after it leaves.
    EXPECT_EQ(pfcore::selectDominantSceneTracks(tracks, 0.3, 0.6, {true, false}),
              (std::vector<bool>{false, false}));
}

TEST(DominantPerson, SimilarCoVisiblePeopleDoNotBecomeOneLeadIdentity)
{
    std::vector<pfcore::PersonTrack> tracks(3);
    for (std::size_t track = 0; track < tracks.size(); ++track) {
        for (int i = 0; i < (track == 1 ? 3 : 4); ++i) {
            auto observation = person(i * 0.1 + (track == 2 ? 1 : 0), track * 40, .9);
            observation.faceEmbedding = {1,0};
            tracks[track].observations.push_back(observation);
        }
    }
    // Track 2 continues track 0 later; track 1 is a different simultaneously
    // visible actor. A non-overlapping bridge must not absorb that actor.
    EXPECT_EQ(pfcore::selectDominantSceneTracks(tracks, 0, 2),
              (std::vector<bool>{true,false,true}));
}

TEST(DominantPerson, SourceLeadMaskSkipsExtraOnlyShotsButKeepsReturningLead)
{
    std::vector<pfcore::PersonTrack> tracks(4);
    for (std::size_t track = 0; track < tracks.size(); ++track) {
        for (int sample = 0; sample < 4; ++sample) {
            auto observation = person(track + sample * .1, 0, .9);
            // Two separate shots of the same extra are still ineligible.
            observation.faceEmbedding = (track == 0 || track == 3)
                ? std::vector<float>{1, 0} : std::vector<float>{0, 1};
            tracks[track].observations.push_back(observation);
        }
    }
    const auto lead = pfcore::selectDominantSceneTracks(tracks, 0, 4);
    ASSERT_EQ(lead, (std::vector<bool>{true, false, false, true}));
    EXPECT_EQ(pfcore::selectDominantSceneTracks(tracks, 1, 2, lead),
              (std::vector<bool>{false, false, false, false}));
    EXPECT_EQ(pfcore::selectDominantSceneTracks(tracks, 2, 3, lead),
              (std::vector<bool>{false, false, false, false}));
    EXPECT_EQ(pfcore::selectDominantSceneTracks(tracks, 3, 4, lead),
              (std::vector<bool>{false, false, false, true}));
}

TEST(DominantPerson, InsufficientLeadObservationsDoNotPromoteAnUnverifiedExtra)
{
    std::vector<pfcore::PersonTrack> tracks(2);
    tracks[0].observations.push_back(person(0, 0, .9));
    for (int sample = 0; sample < 4; ++sample)
        tracks[1].observations.push_back(person(sample * .1, 40, .9));
    EXPECT_EQ(pfcore::selectDominantSceneTracks(tracks, 0, 1, {true, false}),
              (std::vector<bool>{false, false}));
    EXPECT_EQ(pfcore::selectDominantSceneTracks(tracks, 0, 1, {false, false}),
              (std::vector<bool>{false, false}));
    // An omitted mask still allows standalone shot-local selection.
    EXPECT_EQ(pfcore::selectDominantSceneTracks(tracks, 0, 1),
              (std::vector<bool>{false, true}));
}

TEST(DominantPerson, SceneCutSeparatesIdenticalGeometryWithoutAnotherInference)
{
    pfcore::PersonTracker tracker;
    for (int i = 0; i < 8; ++i)
        tracker.update(i * .1, .1, {person(i * .1, 0, .9)});
    ASSERT_EQ(tracker.tracks().size(), 1U);
    tracker.retrackScenes({.4});
    ASSERT_EQ(tracker.tracks().size(), 2U);
    EXPECT_EQ(tracker.tracks()[0].observations.size(), 4U);
    EXPECT_EQ(tracker.tracks()[1].observations.size(), 4U);
    EXPECT_LT(tracker.tracks()[0].observations.back().timestampSeconds, .4);
    EXPECT_DOUBLE_EQ(tracker.tracks()[1].observations.front().timestampSeconds, .4);
}

TEST(DominantPerson, SceneRetrackingPreservesSamePersonIdentityAcrossShots)
{
    pfcore::PersonTracker tracker;
    for (int i = 0; i < 8; ++i) {
        auto detection = person(i * .1, 0, .9);
        detection.faceEmbedding = {1, 0};
        tracker.update(i * .1, .1, {detection});
    }
    tracker.retrackScenes({.4});
    ASSERT_EQ(tracker.tracks().size(), 2U);
    EXPECT_EQ(pfcore::selectDominantSceneTracks(tracker.tracks(), 0, 1),
              (std::vector<bool>{true, true}));
    EXPECT_DOUBLE_EQ(tracker.tracks()[0].totalTimeSeconds(), .4);
    EXPECT_DOUBLE_EQ(tracker.tracks()[1].totalTimeSeconds(), .4);
}

TEST(DominantPerson, InvalidSceneBoundariesDoNotDestroyTracks)
{
    pfcore::PersonTracker tracker;
    tracker.update(0, .1, {person(0, 0, .9)});
    EXPECT_THROW(tracker.retrackScenes({2, 1}), std::invalid_argument);
    ASSERT_EQ(tracker.tracks().size(), 1U);
    EXPECT_EQ(tracker.tracks()[0].observations.size(), 1U);
    tracker.retrackScenes({});
    EXPECT_EQ(tracker.tracks().size(), 1U);
}

TEST(DominantPerson, BriefRecognisableExtraDoesNotOverrideLongerLead)
{
    std::vector<pfcore::IdentitySummary> people(2);
    people[0].duration = 10;
    people[0].area = 1;
    people[1].duration = 1;
    people[1].area = 5;
    people[1].face = {1, 0};
    people[1].faceEvidence = 1;
    EXPECT_EQ(pfcore::selectDominantIdentities(people), (std::vector<bool>{true, false}));
}

TEST(DominantPerson, FragmentationDoesNotInflateLeadProminence)
{
    std::vector<pfcore::IdentitySummary> people(3);
    people[0].duration = 2;
    people[0].area = 3;
    people[0].face = {1, 0};
    people[0].faceEvidence = 1;
    for (int i = 1; i < 3; ++i) {
        people[i].duration = 1;
        people[i].area = 2;
        people[i].face = {0, 1};
        people[i].faceEvidence = 1;
    }
    // Two fragments have the same total duration, but summing their average
    // areas used to wrongly make this smaller secondary person the lead.
    EXPECT_EQ(pfcore::selectDominantIdentities(people), (std::vector<bool>{true, false, false}));
}

TEST(DominantPerson, SingleContradictoryFaceVetoesAClothingBridge)
{
    std::vector<pfcore::IdentitySummary> people(3);
    people[0].duration = 10;
    people[0].body = {1, 0};
    people[0].bodyEvidence = 1;
    people[0].face = {1, 0};
    people[0].faceEvidence = 1;
    people[1] = people[0];
    people[1].duration = 1;
    people[1].face = {0, 1};
    people[1].faceEvidence = 1.0 / 3.0;
    // A face-free intermediate must not hide the extra's conflicting face.
    people[2] = people[0];
    people[2].duration = 1;
    people[2].face.clear();
    people[2].faceEvidence = 0;
    EXPECT_EQ(pfcore::selectDominantIdentities(people),
              (std::vector<bool>{true, false, true}));
    // A sparse consistent face may still use verified body evidence.
    people[1].face = {1, 0};
    EXPECT_EQ(pfcore::selectDominantIdentities(people),
              (std::vector<bool>{true, true, true}));
}

} // namespace

TEST(DominantPerson, ClothingChainNeedsDirectEvidenceToAFaceAnchor)
{
    std::vector<pfcore::IdentitySummary> people(3);
    people[0].duration = 10;
    people[0].face = {1, 0}; people[0].faceEvidence = 1;
    people[0].body = {1, 0}; people[0].bodyEvidence = 1;
    people[1].duration = 2;
    people[1].body = {.8660254F, .5F}; people[1].bodyEvidence = 1;
    people[2].duration = 1;
    people[2].body = {.5F, .8660254F}; people[2].bodyEvidence = 1;
    // Both neighbouring clothing similarities exceed .76; the third person
    // has no direct evidence to the known lead (.5) and must not be imported.
    EXPECT_EQ(pfcore::selectDominantIdentities(people), (std::vector<bool>{true, true, false}));
    std::swap(people[1], people[2]);
    EXPECT_EQ(pfcore::selectDominantIdentities(people), (std::vector<bool>{true, false, true}));
}

namespace {
std::vector<pfcore::PersonTrack> profileFragments()
{
    std::vector<pfcore::PersonTrack> identities(5);
    const double angles[] = {0, 20, 30, 80, 120};
    for (std::size_t i = 0; i < identities.size(); ++i) {
        const double radians = angles[i] * std::acos(-1.0) / 180;
        identities[i].id = i + 1;
        for (int sample = 0; sample < 3; ++sample) {
            auto observation = person(i * 10 + sample * .5, 0, .9);
            observation.faceEmbedding = {static_cast<float>(std::cos(radians)),
                                         static_cast<float>(std::sin(radians))};
            observation.appearanceEmbedding = {1, 0};
            observation.frameDurationSeconds = (i < 3 ? 10.0 : 1.0) / 3;
            identities[i].observations.push_back(std::move(observation));
        }
    }
    return identities;
}
}

TEST(DominantPerson, ProfileRecoveryUsesFaceConsensusAndIndependentBodyEvidence)
{
    const auto identities = profileFragments();
    // Profile 80 disagrees with the front-facing anchor, but has two direct
    // positive face links and corroborating body evidence to the original lead.
    EXPECT_EQ(pfcore::selectDominantSourceTracks(identities).tracks,
              (std::vector<bool>{true, true, true, true, false}));
}

TEST(DominantPerson, ProfileRecoveryDoesNotUseRecoveredFragmentsAsAnchors)
{
    auto identities = profileFragments();
    // 120 matches 80 and its clothing, but not the original face consensus.
    std::swap(identities[3], identities[4]);
    EXPECT_EQ(pfcore::selectDominantSourceTracks(identities).tracks,
              (std::vector<bool>{true, true, true, false, true}));
}

TEST(DominantPerson, ProfileRecoveryRequiresRepeatedFaceAndCorroboratingBody)
{
    auto identities = profileFragments();
    for (std::size_t i = 1; i < 3; ++i) identities[3].observations[i].faceEmbedding.clear();
    EXPECT_EQ(pfcore::selectDominantSourceTracks(identities).tracks,
              (std::vector<bool>{true, true, true, false, false}));
    identities = profileFragments();
    for (auto& observation : identities[3].observations) observation.appearanceEmbedding = {0, 1};
    EXPECT_EQ(pfcore::selectDominantSourceTracks(identities).tracks,
              (std::vector<bool>{true, true, true, false, false}));
}

TEST(DominantPerson, ProfileRecoveryCannotJoinACoVisibleLeadOrAmbiguousNewPeople)
{
    auto identities = profileFragments();
    for (std::size_t i = 0; i < 3; ++i)
        identities[3].observations[i].timestampSeconds = identities[0].observations[i].timestampSeconds;
    EXPECT_EQ(pfcore::selectDominantSourceTracks(identities).tracks,
              (std::vector<bool>{true, true, true, false, false}));
    identities = profileFragments();
    identities[4] = identities[3];
    // Both new detections pass the same independent evidence. They coexist,
    // so neither is silently chosen through order-dependent admission.
    EXPECT_EQ(pfcore::selectDominantSourceTracks(identities).tracks,
              (std::vector<bool>{true, true, true, false, false}));
}

TEST(DominantPerson, ProfileRecoveryDoesNotExtrapolateIntoUnverifiedPrefixOrTail)
{
    auto tracks = profileFragments();
    auto prefix = tracks[3].observations.front();
    prefix.timestampSeconds -= .5;
    prefix.faceEmbedding.clear();
    auto tail = tracks[3].observations.back();
    tail.timestampSeconds += .5;
    tail.faceEmbedding.clear();
    tracks[3].observations.insert(tracks[3].observations.begin(), prefix);
    tracks[3].observations.push_back(tail);
    const auto selection = pfcore::selectDominantSourceTracks(tracks);
    ASSERT_TRUE(selection.recovered[3]);
    ASSERT_EQ(selection.observationRuns[3].size(), 1U);
    EXPECT_EQ(selection.observationRuns[3][0].begin, 1U);
    EXPECT_EQ(selection.observationRuns[3][0].end, 4U);
}

TEST(DominantPerson, ProfileRecoverySplitsAtAnObservedIdentityConflict)
{
    auto tracks = profileFragments();
    const auto positive = tracks[3].observations.front();
    tracks[3].observations.clear();
    for (int sample = 0; sample < 9; ++sample) {
        auto observation = positive;
        observation.timestampSeconds = 30 + sample * .5;
        observation.frameDurationSeconds = .1;
        if (sample == 4) observation.faceEmbedding = {-1, 0};
        tracks[3].observations.push_back(std::move(observation));
    }
    const auto selection = pfcore::selectDominantSourceTracks(tracks);
    ASSERT_TRUE(selection.recovered[3]);
    ASSERT_EQ(selection.observationRuns[3].size(), 2U);
    EXPECT_EQ(selection.observationRuns[3][0].begin, 0U);
    EXPECT_EQ(selection.observationRuns[3][0].end, 4U);
    EXPECT_EQ(selection.observationRuns[3][1].begin, 5U);
    EXPECT_EQ(selection.observationRuns[3][1].end, 9U);
}
