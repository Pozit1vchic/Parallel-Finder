#include <gtest/gtest.h>
#include <cmath>
#include <algorithm>
#include <thread>

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

TEST(DominantPerson, ConflictingDuplicateCannotContinueLeadThroughUnsampledFrames)
{
    pfcore::PersonTracker tracker;
    auto lead = person(0, 0, .9);
    lead.keypoints = {{4,2,.9},{3,2,.9},{5,2,.9},{2,3,.9},{6,3,.9},{2,6,.9},{7,6,.9}};
    lead.faceEmbedding = {1,0};
    tracker.update(0, .1, {lead});
    tracker.update(.1, .1, {lead});
    auto other = lead; other.faceEmbedding = {0,1}; other.confidence = .7;
    auto duplicate = other; duplicate.faceEmbedding.clear(); duplicate.confidence = .31;
    duplicate.box = {-.5,0,9.5,20};
    tracker.update(.2, .1, {duplicate,other});
    other.faceEmbedding.clear();
    tracker.update(.3, .1, {duplicate,other});
    ASSERT_EQ(tracker.tracks()[0].observations.size(), 2U);
    std::size_t samples = 0;
    for (const auto& track : tracker.tracks()) samples += track.observations.size();
    EXPECT_EQ(samples, 6U); // all actual detections, no invented identity crops
    tracker.retrackScenes({.15});
    samples = 0;
    for (const auto& track : tracker.tracks()) samples += track.observations.size();
    EXPECT_EQ(samples, 6U);
}

TEST(DominantPerson, OverlappingOtherDoesNotRetireVisibleLead)
{
    pfcore::PersonTracker tracker;
    auto lead = person(0, 0, .9); lead.faceEmbedding = {1,0};
    tracker.update(0, .1, {lead});
    auto other = person(.1, 1, .95); other.faceEmbedding = {0,1};
    tracker.update(.1, .1, {other,lead});
    lead.faceEmbedding.clear();
    tracker.update(.2, .1, {lead});
    ASSERT_EQ(tracker.tracks()[0].observations.size(), 3U);
}

TEST(DominantPerson, IndependentIdentityConflictEndsTrackBeforeUnsampledContinuation)
{
    pfcore::PersonTracker tracker;
    auto lead = person(0,0,.9); lead.faceEmbedding = {1,0};
    tracker.update(0,.1,{lead});
    auto other = lead; other.faceEmbedding = {0,1};
    tracker.update(.1,.1,{other});
    other.faceEmbedding.clear();
    tracker.update(.2,.1,{other});
    ASSERT_EQ(tracker.tracks().size(),2U);
    EXPECT_EQ(tracker.tracks()[0].observations.size(),1U);
    EXPECT_EQ(tracker.tracks()[1].observations.size(),2U);
}


} // namespace

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

TEST(DominantPerson, CorrectedAssociationCannotAdmitAnUnverifiedRc16RejectedTrack)
{
    const auto current = profileFragments();
    auto previous = current;
    // This former extra belongs to the new component, but lacked independent
    // repeated-face support. A changed component alone must not admit it.
    auto extra = current[0];
    extra.id = 99;
    for (auto& observation : extra.observations) observation.timestampSeconds += 100;
    extra.observations[1].faceEmbedding.clear();
    extra.observations[2].faceEmbedding.clear();
    auto corrected = current;
    corrected.push_back(extra);
    auto oldExtra = extra;
    pfcore::PersonTrack foreign;
    foreign.id = 100;
    for (int sample = 0; sample < 5; ++sample) {
        auto observation = extra.observations.back();
        observation.timestampSeconds += 2 + sample * .2;
        observation.faceEmbedding = {-1, 0};
        observation.appearanceEmbedding = {-1, 0};
        foreign.observations.push_back(observation);
        oldExtra.observations.push_back(observation);
    }
    corrected.push_back(foreign);
    previous.push_back(oldExtra);
    // Identical raw observations, with only association changed.
    EXPECT_TRUE(pfcore::selectDominantSourceTracks(corrected).tracks[current.size()]);
    EXPECT_FALSE(pfcore::selectDominantSourceTracks(corrected, &previous).tracks[current.size()]);
}

TEST(DominantPerson, Rc16AdmissionPreservesOnlyTheOriginallyAdmittedObservationRange)
{
    auto current = profileFragments();
    auto previous = current;
    // The same raw tail belonged to a rejected fragment in the old association.
    auto tail = previous[0].observations.back();
    previous[0].observations.pop_back();
    tail.faceEmbedding.clear();
    current[0].observations.back() = tail;
    previous.push_back({99, {tail}});
    const auto selection = pfcore::selectDominantSourceTracks(current, &previous);
    ASSERT_TRUE(selection.tracks[0]);
    ASSERT_EQ(selection.observationRuns[0].size(), 1U);
    EXPECT_EQ(selection.observationRuns[0][0].begin, 0U);
    EXPECT_EQ(selection.observationRuns[0][0].end, 2U);
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

namespace {
std::vector<pfcore::MotionWindow> videoIdentity(const std::string& source,
    std::vector<float> face, double duration=10)
{
    std::vector<pfcore::MotionWindow> windows;
    for (std::size_t shot=0;shot<2;++shot) {
        pfcore::MotionWindow w;w.sourceId=source;w.hasSceneIndex=true;w.sceneIndex=shot;
        w.faceEmbedding=face;w.faceConfidence=1;
        w.sceneStartSeconds=shot*duration;w.sceneEndSeconds=(shot+1)*duration;
        w.frames={pfcore::PoseFrame{w.sceneStartSeconds,{}},pfcore::PoseFrame{w.sceneEndSeconds-.1,{}}};
        windows.push_back(std::move(w));
    }
    return windows;
}
}

TEST(DominantPerson, SharedPersonAcrossFilesOutvotesALongerForeignVideo)
{
    auto windows=videoIdentity("short-a",{1,0});
    const auto b=videoIdentity("short-b",{.98F,.1F});
    const auto foreign=videoIdentity("long-foreign",{0,1},1000);
    windows.insert(windows.end(),b.begin(),b.end());windows.insert(windows.end(),foreign.begin(),foreign.end());
    const auto selection=pfcore::selectDominantVideoWindows(windows);
    EXPECT_EQ(selection.sources,(std::vector<std::string>{"short-a","short-b"}));
    EXPECT_EQ(selection.windows,(std::vector<bool>{true,true,true,true,false,false}));
    std::reverse(windows.begin(),windows.end());
    const auto reversed=pfcore::selectDominantVideoWindows(windows);
    EXPECT_EQ(reversed.sources,selection.sources);
    EXPECT_EQ(reversed.windows,(std::vector<bool>{false,false,true,true,true,true}));
}

TEST(DominantPerson, SharedPersonDoesNotBridgeConflictingFacesOrUseLocalTimestamps)
{
    auto windows=videoIdentity("a",{1,0});
    const auto b=videoIdentity("b",{.707F,.707F});
    const auto c=videoIdentity("c",{0,1});
    windows.insert(windows.end(),b.begin(),b.end());windows.insert(windows.end(),c.begin(),c.end());
    const auto selection=pfcore::selectDominantVideoWindows(windows);
    EXPECT_EQ(selection.sources.size(),2U);
    EXPECT_FALSE(selection.windows[0] && selection.windows[4]);
    // All files start at time zero, which cannot mean co-visible people.
    EXPECT_TRUE(selection.windows[2]);
}

TEST(DominantPerson, SharedPersonNeedsIndependentShotsAndDoesNotCountSlidingWindows)
{
    auto windows=videoIdentity("good",{1,0},20);
    auto weak=videoIdentity("weak",{1,0},10);weak.resize(1);
    for (int i=0;i<100;++i) windows.push_back(weak[0]);
    const auto selected=pfcore::selectDominantVideoWindows(windows);
    EXPECT_EQ(selected.sources,(std::vector<std::string>{"good"}));
    EXPECT_TRUE(selected.windows[0]);EXPECT_FALSE(selected.windows.back());
    auto single=videoIdentity("one",{});
    EXPECT_EQ(pfcore::selectDominantVideoWindows(single).windows,(std::vector<bool>{true,true}));
}

TEST(DominantPerson, IdentitySelectionReportsCallerProgressAndCancelsWithoutPartialAdmission)
{
    std::vector<pfcore::IdentitySummary> people(100);
    for(std::size_t i=0;i<people.size();++i) {
        auto& p=people[i];p.face={1,0};p.faceEvidence=1;p.duration=1;p.area=1;p.observationTimes={double(i*2)};
    }
    const auto expected=pfcore::selectDominantIdentities(people);
    const auto caller=std::this_thread::get_id();
    pfcore::DominantSelectionControl control;std::size_t reports=0;bool cancelled=false;
    control.cancelled=[&] {return cancelled;};
    control.progress=[&](auto stage,std::size_t done,std::size_t total) {
        EXPECT_EQ(std::this_thread::get_id(),caller);EXPECT_LE(done,total);++reports;
        if(stage==pfcore::DominantSelectionStage::Link && done>=20)cancelled=true;
    };
    const auto stopped=pfcore::selectDominantIdentities(people,control);
    EXPECT_TRUE(cancelled);EXPECT_GT(reports,1U);EXPECT_EQ(std::count(stopped.begin(),stopped.end(),true),0);
    control.progress=[&](auto,std::size_t done,std::size_t total) {EXPECT_LE(done,total);};cancelled=false;
    EXPECT_EQ(pfcore::selectDominantIdentities(people,control),expected);
    control.progress=[&](auto stage,std::size_t,std::size_t) {
        if(stage==pfcore::DominantSelectionStage::Cluster)cancelled=true;
    };
    EXPECT_EQ(std::count(expected.begin(),expected.end(),true),100);
    const auto clustered=pfcore::selectDominantIdentities(people,control);
    EXPECT_EQ(std::count(clustered.begin(),clustered.end(),true),0);
}
