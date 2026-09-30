#include <gtest/gtest.h>
#include <pfcore/MotionMatcher.hpp>
#include <pfcore/PoseSupport.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include "dean_aiming_pose_fixture.hpp"
#include "dean_folded_arms_pose_fixture.hpp"
#include "dean_disputed_motion_fixture.hpp"
#include "soldier_head_pose_fixture.hpp"

namespace {

std::vector<pfcore::MotionWindow> recordedPoseFixture(const char* filename, bool staticFrameSet)
{
    QFile file(QString::fromUtf8(PF_TEST_FIXTURE_DIR) + "/" + QString::fromUtf8(filename));
    if (!file.open(QIODevice::ReadOnly)) return {};
    std::vector<pfcore::MotionWindow> windows;
    for (const auto& value : QJsonDocument::fromJson(file.readAll()).array()) {
        pfcore::MotionWindow window;
        window.sourceId = "recorded-shot-" + std::to_string(windows.size());
        window.staticFrameSet = staticFrameSet;
        window.appearanceEmbedding = {1.0F, 0.0F};
        window.appearanceConfidence = 1.0;
        window.sceneContext = {1.0F, 0.0F};
        for (const auto& item : value.toObject().value("frames").toArray()) {
            const auto f = item.toObject();
            pfcore::PoseFrame frame;
            frame.timestampSeconds = f.value("time").toDouble();
            for (const auto& point : f.value("points").toArray()) {
                const auto p = point.toArray();
                frame.keypoints.push_back({p[0].toDouble(), p[1].toDouble(), p[2].toDouble()});
            }
            window.frames.push_back(std::move(frame));
        }
        windows.push_back(std::move(window));
    }
    return windows;
}

TEST(MotionMatcher, ReliableShortAimingPoseIsNotLostToOccludedEndpoints)
{
    const auto windows = recordedPoseFixture("dean-partial-aiming-pose.json", true);
    ASSERT_EQ(windows.size(), 4U);
    auto supported = windows[3];
    const auto runs = pfcore::observedPoseRuns(windows[3]);
    ASSERT_EQ(runs.size(), 1U);
    EXPECT_EQ(runs[0].begin, 2U);
    EXPECT_EQ(runs[0].end, 4U);
    supported.frames.assign(windows[3].frames.begin() + runs[0].begin,
                            windows[3].frames.begin() + runs[0].end + 1);
    pfcore::MotionMatcherParams params;
    params.allowStaticFrames = true;
    params.requireAppearance = true;
    params.mirrorInvariant = true;
    params.similarityThreshold = 0.70;
    params.timeWeight = 0.10;
    params.staticArticulationSimilarityThreshold = 0.82; // quick profile, not generic pose/head threshold
    const auto score = pfcore::MotionMatcher(params).compare(windows[0], supported).similarity;
    EXPECT_GE(score, params.similarityThreshold);
    auto precise = params;
    precise.staticArticulationSimilarityThreshold = 0.92;
    EXPECT_DOUBLE_EQ(pfcore::MotionMatcher(precise).compare(windows[0], supported).similarity, 0.0);
    auto otherPerson = supported;
    otherPerson.appearanceEmbedding = {0.0F, 1.0F};
    EXPECT_DOUBLE_EQ(pfcore::MotionMatcher(params).compare(windows[0], otherPerson).similarity, 0.0);
    EXPECT_DOUBLE_EQ(pfcore::MotionMatcher(params).compare(windows[0], windows[3]).similarity, 0.0);
    auto single = windows[3];
    single.frames.resize(1);
    EXPECT_TRUE(pfcore::observedPoseRuns(single).empty());
}

TEST(MotionMatcher, ReliablePoseRunsDoNotBridgeMissingSamplesOrTimestampGaps)
{
    const auto windows = recordedPoseFixture("dean-partial-aiming-pose.json", true);
    ASSERT_EQ(windows.size(), 4U);
    auto broken = windows[3];
    broken.frames[3].keypoints[9].confidence = 0.0;
    EXPECT_TRUE(pfcore::observedPoseRuns(broken).empty());
    broken = windows[3];
    broken.frames[3].timestampSeconds += 1.0;
    EXPECT_TRUE(pfcore::observedPoseRuns(broken).empty());
    broken = windows[3];
    broken.frames[3].timestampSeconds = std::numeric_limits<double>::quiet_NaN();
    EXPECT_TRUE(pfcore::observedPoseRuns(broken).empty());
    broken = windows[3];
    broken.frames[3].keypoints[9].x = std::numeric_limits<double>::quiet_NaN();
    EXPECT_TRUE(pfcore::observedPoseRuns(broken).empty());
}

TEST(MotionMatcher, ObservedArmPoseIsNotReducedToAHeadOnlyMatch)
{
    auto head = soldierHeadPoseFixture()[0];
    auto body = head;
    body.sourceId = "observed-body-shot";
    const auto folded = deanFoldedArmsPoseFixture()[0];
    ASSERT_EQ(folded.frames.front().keypoints.size(), 17U);
    for (auto& frame : body.frames)
        for (std::size_t i = 5; i < 17; ++i)
            frame.keypoints[i] = folded.frames.front().keypoints[i];
    pfcore::MotionMatcherParams params;
    params.allowStaticFrames = true;
    params.requireAppearance = true;
    auto repeat = head;
    repeat.sourceId = "head-repeat";
    ASSERT_GE(pfcore::MotionMatcher(params).compare(head, repeat).similarity, params.similarityThreshold);
    EXPECT_DOUBLE_EQ(pfcore::MotionMatcher(params).compare(head, body).similarity, 0.0);
    EXPECT_DOUBLE_EQ(pfcore::MotionMatcher(params).compare(body, head).similarity, 0.0);
}

TEST(MotionMatcher, StaticBudgetDoesNotStarveVerifiedCloseups)
{
    auto windows = soldierHeadPoseFixture();
    pfcore::MotionMatcherParams params;
    params.allowStaticFrames = true;
    params.requireAppearance = true;
    params.maxUniqueResults = 3;
    const auto head = pfcore::MotionMatcher(params).compare(windows[0], windows[1]);
    ASSERT_TRUE(head.headOnlyComparison);
    ASSERT_GE(head.similarity, params.similarityThreshold);
    for (int identity = 1; identity <= 4; ++identity) {
        auto body = deanAimingPoseFixture()[0];
        body.faceEmbedding.assign(5, 0.0F);
        body.faceEmbedding[identity] = 1.0F;
        body.faceConfidence = 1.0;
        body.appearanceEmbedding.clear();
        for (int side = 0; side < 2; ++side) {
            body.sourceId = "body-" + std::to_string(identity) + "-" + std::to_string(side);
            windows.push_back(body);
        }
    }
    // Equal dimensions are required for the synthetic identity gate.
    windows[0].faceEmbedding = windows[1].faceEmbedding = {1,0,0,0,0};
    const auto matches = pfcore::MotionMatcher(params).findAllPairs(windows);
    ASSERT_EQ(matches.size(), 3u);
    EXPECT_EQ(std::count_if(matches.begin(), matches.end(), [](const auto& match) {
        return match.headOnlyComparison;
    }), 1);
    for (const auto& match : matches) {
        EXPECT_TRUE(match.faceVerified);
        if (match.headOnlyComparison) { EXPECT_DOUBLE_EQ(match.similarity, head.similarity); }
    }
    auto duplicated = windows;
    for (int i = 0; i < 4; ++i) {
        duplicated.push_back(windows[0]);
        duplicated.push_back(windows[1]);
    }
    params.maxUniqueResults = 4;
    for (int order = 0; order < 2; ++order) {
        const auto selected = pfcore::MotionMatcher(params).findAllPairs(duplicated);
        ASSERT_EQ(selected.size(), 4u); // duplicate closeups cannot waste slots
        EXPECT_EQ(std::count_if(selected.begin(), selected.end(), [](const auto& match) {
            return match.headOnlyComparison;
        }), 1);
        EXPECT_TRUE(std::is_sorted(selected.begin(), selected.end(), [](const auto& a, const auto& b) {
            return a.similarity > b.similarity;
        }));
        std::reverse(duplicated.begin(), duplicated.end());
    }
    // A full pose pool must still fill the whole unchanged budget.
    windows.erase(windows.begin(), windows.begin() + 2);
    EXPECT_EQ(pfcore::MotionMatcher(params).findAllPairs(windows).size(), 4u);
}

TEST(MotionMatcher, SameShotWindowsCannotCrowdOutVerifiedDifferentShot)
{
    const auto poses = deanAimingPoseFixture();
    auto left = poses[0], right = poses[0];
    for (auto& frame : right.frames) {
        frame.timestampSeconds += 20.0;
        frame.keypoints[10].x += 35.0;
    }
    left.sourceId = right.sourceId = "montage";
    left.hasSceneIndex = right.hasSceneIndex = true;
    left.sceneIndex = 1;
    right.sceneIndex = 2;
    left.sceneContext = {1.0F, 0.0F};
    right.sceneContext = {0.0F, 1.0F};
    left.sceneStartSeconds = 2.5;
    left.sceneEndSeconds = 3.3;
    right.sceneStartSeconds = 22.5;
    right.sceneEndSeconds = 23.3;
    left.faceEmbedding = {1.0F, 0.0F};
    right.faceEmbedding = {0.96F, 0.28F};
    left.faceConfidence = right.faceConfidence = 1.0;
    left.appearanceEmbedding.clear();
    right.appearanceEmbedding.clear();
    pfcore::MotionMatcherParams params;
    params.allowStaticFrames = true;
    params.requireAppearance = true;
    const pfcore::MotionMatcher matcher(params);
    ASSERT_GE(matcher.compare(left, right).similarity, params.similarityThreshold);
    std::vector<pfcore::MotionWindow> windows;
    for (int i = 0; i < 140; ++i) windows.push_back(left);
    for (int i = 0; i < 140; ++i) windows.push_back(right);
    // Each nearest-neighbour list used to be entirely filled by the SAME
    // shot. The later same-shot rejection then left no useful candidate.
    const auto matches = matcher.findAllPairs(windows);
    ASSERT_EQ(matches.size(), 1u);
    EXPECT_TRUE(matches.front().faceVerified);
    EXPECT_NE(windows[matches.front().leftIndex].sceneIndex,
              windows[matches.front().rightIndex].sceneIndex);
    std::reverse(windows.begin(), windows.end());
    EXPECT_EQ(matcher.findAllPairs(windows).size(), 1u);
    for (auto& window : windows)
        if (window.sceneIndex == 2) window.faceEmbedding = {0.0F, 1.0F};
    EXPECT_TRUE(matcher.findAllPairs(windows).empty());
}

TEST(MotionMatcher, ContradictoryPeopleCannotConsumePoseRetrievalBudget)
{
    auto left = deanAimingPoseFixture()[0], right = left;
    constexpr int identities = 322;
    const auto identify = [&](pfcore::MotionWindow& window, int identity) {
        window.faceEmbedding.assign(identities, 0.0F);
        window.faceEmbedding[identity] = 1.0F;
        window.faceConfidence = 1.0;
        window.appearanceEmbedding.clear();
    };
    identify(left, 0);
    identify(right, 0);
    right.faceEmbedding[0] = 0.96F;
    right.faceEmbedding[1] = 0.28F;
    left.sourceId = right.sourceId = "crowded-montage";
    left.hasSceneIndex = right.hasSceneIndex = true;
    left.sceneIndex = 1; right.sceneIndex = 2;
    left.sceneContext = {1,0}; right.sceneContext = {0,1};
    for (auto& frame : right.frames) {
        frame.timestampSeconds += 20;
        frame.keypoints[10].x += 35;
    }
    left.sceneStartSeconds = 2.5; left.sceneEndSeconds = 3.3;
    right.sceneStartSeconds = 22.5; right.sceneEndSeconds = 23.3;
    pfcore::MotionMatcherParams params;
    params.allowStaticFrames = true;
    params.requireAppearance = true;
    const pfcore::MotionMatcher matcher(params);
    ASSERT_GE(matcher.compare(left, right).similarity, params.similarityThreshold);
    std::vector<pfcore::MotionWindow> windows{left, right};
    // Pose neighbours: many more similar poses belonging to OTHER people.
    for (int i = 0; i < 320; ++i) {
        auto distractor = i % 2 ? left : right;
        identify(distractor, i + 2);
        distractor.sceneIndex = i + 10;
        windows.push_back(std::move(distractor));
    }
    // Identity neighbours: the actor in a contradictory posture. They fill
    // face ANN budgets; the positive must therefore survive pose retrieval.
    for (int side = 0; side < 2; ++side) {
        auto distractor = side ? right : left;
        distractor.sceneIndex = side + 3;
        for (auto& frame : distractor.frames) {
            frame.timestampSeconds += 100;
            frame.keypoints[9] = frame.keypoints[0];
            frame.keypoints[10] = frame.keypoints[0];
        }
        ASSERT_LT(matcher.compare(left, distractor).similarity, params.similarityThreshold);
        for (int i = 0; i < 140; ++i) windows.push_back(distractor);
    }
    for (int order = 0; order < 2; ++order) {
        const auto found = matcher.findAllPairs(windows);
        EXPECT_TRUE(std::any_of(found.begin(), found.end(), [&](const auto& match) {
            const auto a = windows[match.leftIndex].sceneIndex;
            const auto b = windows[match.rightIndex].sceneIndex;
            return (a == 1 && b == 2) || (a == 2 && b == 1);
        }));
        std::reverse(windows.begin(), windows.end());
    }
}

TEST(MotionMatcher, OccludedOppositeWristCannotHideContradictoryObservedArm)
{
    pfcore::MotionWindow left;
    left.sourceId = "visible-arm";
    left.staticFrameSet = true;
    left.appearanceEmbedding = {1.0F};
    left.appearanceConfidence = 1.0;
    const std::vector<pfcore::Keypoint> points = {
        {0,-1}, {-.1,-1.1}, {.1,-1.1}, {-.2,-1}, {.2,-1},
        {-.5,0}, {.5,0}, {-.5,.7}, {.5,.7}, {-.5,1.4}, {.5,1.4,0},
        {-.3,1}, {.3,1}, {-.3,2}, {.3,2}, {-.3,3}, {.3,3}
    };
    for (int i = 0; i < 5; ++i) left.frames.push_back({i / 6.0, points});
    auto right = left;
    right.sourceId = "different-visible-arm";
    for (auto& frame : right.frames) frame.keypoints[9] = {-2,-2};
    pfcore::MotionMatcherParams params;
    params.allowStaticFrames = true;
    EXPECT_LT(pfcore::MotionMatcher(params).compare(left, right).similarity,
              params.similarityThreshold);
}

TEST(MotionMatcher, DisputedDeanMotionDoesNotPassOnWeakDirectionalAgreement)
{
    const auto poses = deanDisputedMotionFixture();
    pfcore::MotionMatcherParams params;
    params.similarityThreshold = 0.70; // quick-search acceptance, not a stricter test profile
    params.mirrorInvariant = true;
    const pfcore::MotionMatcher matcher(params);
    for (const auto& [a,b] : {std::pair{0,1}, std::pair{0,2}, std::pair{2,3}})
        EXPECT_LT(matcher.compare(poses[a], poses[b]).similarity, params.similarityThreshold);
    // Retain genuine trajectory evidence rather than fixing precision by
    // disabling motion: each recorded observation still matches its repeat.
    for (const auto& pose : poses) {
        auto repeat = pose;
        repeat.sourceId += "-repeat";
        EXPECT_GT(matcher.compare(pose, repeat).similarity, params.similarityThreshold);
    }
}

TEST(MotionMatcher, FoldedArmsCannotMatchAnOccludedAimingBodyMotion)
{
    const auto poses = recordedPoseFixture("dean-folded-vs-aiming-motion.json", false);
    ASSERT_EQ(poses.size(), 2U);
    pfcore::MotionMatcherParams params;
    params.similarityThreshold = 0.70;
    params.minTemporalFrames = 6;
    params.timeWeight = 0.10;
    params.mirrorInvariant = true;
    const pfcore::MotionMatcher matcher(params);
    EXPECT_LT(matcher.compare(poses[0], poses[1]).similarity, params.similarityThreshold);
    auto repeat = poses[0];
    repeat.sourceId += "-repeat";
    EXPECT_GT(matcher.compare(poses[0], repeat).similarity, params.similarityThreshold);
}

TEST(MotionMatcher, SimilarElbowAnglesDoNotConfuseFoldedArmsWithAiming)
{
    pfcore::MotionMatcherParams params;
    params.allowStaticFrames = true;
    params.mirrorInvariant = true;
    const pfcore::MotionMatcher matcher(params);
    for (const auto& aiming : deanAimingPoseFixture())
        for (const auto& folded : deanFoldedArmsPoseFixture())
            EXPECT_LT(matcher.compare(aiming, folded).similarity, params.similarityThreshold);
}

TEST(MotionMatcher, ArticulatedStaticPoseSurvivesChangedViewAndOcclusion)
{
    const auto poses = deanAimingPoseFixture();
    pfcore::MotionMatcherParams params;
    params.allowStaticFrames = true;
    params.mirrorInvariant = true;
    const pfcore::MotionMatcher matcher(params);
    for (std::size_t i = 0; i < poses.size(); ++i)
        for (std::size_t j = i + 1; j < poses.size(); ++j) {
            SCOPED_TRACE(std::to_string(i) + "/" + std::to_string(j));
            EXPECT_GE(matcher.compare(poses[i], poses[j]).similarity, params.similarityThreshold);
        }
}

TEST(MotionMatcher, ArticulationNeedsIdentityAndSustainedObservedLimbs)
{
    auto poses = deanAimingPoseFixture();
    pfcore::MotionMatcherParams params;
    params.allowStaticFrames = true;
    params.mirrorInvariant = true;
    const pfcore::MotionMatcher matcher(params);
    auto changed = poses[1];
    changed.appearanceEmbedding = {0.0F, 1.0F};
    EXPECT_DOUBLE_EQ(matcher.compare(poses[0], changed).similarity, 0.0);
    changed.appearanceEmbedding.clear();
    EXPECT_DOUBLE_EQ(matcher.compare(poses[0], changed).similarity, 0.0);
    changed = poses[1];
    for (std::size_t i = 2; i < changed.frames.size(); ++i)
        changed.frames[i].keypoints[7].confidence = 0.0;
    EXPECT_DOUBLE_EQ(matcher.compare(poses[0], changed).similarity, 0.0);
    changed = poses[1];
    for (auto& frame : changed.frames) {
        // A hand at the face is not an extended two-handed posture.
        frame.keypoints[9].x = frame.keypoints[0].x;
        frame.keypoints[9].y = frame.keypoints[0].y + 3;
    }
    EXPECT_LT(matcher.compare(poses[0], changed).similarity, params.similarityThreshold);
    params.mirrorInvariant = false;
    EXPECT_DOUBLE_EQ(pfcore::MotionMatcher(params).compare(poses[0], poses[1]).similarity, 0.0);
    EXPECT_NEAR(matcher.compare(poses[0], poses[1]).similarity,
                matcher.compare(poses[1], poses[0]).similarity, 1e-9);
}

TEST(MotionMatcher, ObservedHandAtNoseIsContradictionNotMissingEvidence)
{
    auto poses = deanAimingPoseFixture();
    auto changed = poses[2];
    changed.sourceId = "hand-at-face";
    for (auto& frame : changed.frames) {
        frame.keypoints[9] = frame.keypoints[0];
    }
    pfcore::MotionMatcherParams params;
    params.allowStaticFrames = true;
    EXPECT_LT(pfcore::MotionMatcher(params).compare(poses[2], changed).similarity,
              params.similarityThreshold);
}

TEST(MotionMatcher, StaticPoseScoreDoesNotDependOnBackgroundColors)
{
    auto poses = deanAimingPoseFixture();
    pfcore::MotionMatcherParams params;
    params.allowStaticFrames = true;
    params.mirrorInvariant = true;
    const pfcore::MotionMatcher matcher(params);
    const double baseline = matcher.compare(poses[0], poses[1]).similarity;
    poses[0].sceneContext = {1.0F, 0.0F};
    poses[1].sceneContext = {0.0F, 1.0F};
    EXPECT_NEAR(matcher.compare(poses[0], poses[1]).similarity, baseline, 1e-9);
    EXPECT_EQ(matcher.findAllPairs(poses).size(), 3U);
}

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

pfcore::MotionWindow gestureWindow(const char* source, double offset, bool inverted)
{
    pfcore::MotionWindow result;
    result.sourceId = source;
    for (int i = 0; i < 30; ++i) {
        const double phase = static_cast<double>(i) / 29.0;
        const double arm = inverted ? 1.0 - phase : phase;
        const double leg = inverted ? phase : 1.0 - phase;
        result.frames.push_back({offset + i / 12.0,
                                 {{0.0, 0.0}, {0.0, 1.0},
                                  {0.65 * arm, 0.32}, {-0.55 * arm, 0.36},
                                  {0.32, 1.0 + 0.45 * leg},
                                  {-0.32, 1.0 + 0.45 * leg}}});
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


TEST(MotionMatcher, MirrorInvariantComparisonRecoversReflectedMotion)
{
    const auto left = window("left", 0.0, false);
    const auto right = window("right", 4.0, true);
    const auto plain = pfcore::MotionMatcher().compare(left, right);
    pfcore::MotionMatcherParams params;
    params.mirrorInvariant = true;
    const auto mirrored = pfcore::MotionMatcher(params).compare(left, right);
    EXPECT_LT(plain.similarity, 0.1);
    EXPECT_GT(mirrored.similarity, 0.8);
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

TEST(MotionMatcher, RejectsTrackIdReusedAcrossSceneBoundaryWithoutReId)
{
    pfcore::MotionMatcherParams params;
    params.candidateThreshold = 0.0;
    params.similarityThreshold = 0.1;
    params.minRepeatGapSec = 0.0;
    params.sameFileGapSec = 0.0;
    params.maxUniqueResults = 10;
    auto left = window("same", 0.0);
    auto right = window("same", 8.0);
    left.trackId = right.trackId = 7;
    left.hasSceneIndex = right.hasSceneIndex = true;
    left.sceneIndex = 1;
    right.sceneIndex = 2;
    EXPECT_TRUE(pfcore::MotionMatcher(params).findAllPairs({left, right}).empty());
    EXPECT_DOUBLE_EQ(pfcore::MotionMatcher(params).compare(left, right).similarity, 0.0);
}

TEST(MotionMatcher, DoesNotMergeDifferentTracksEvenWithHighAppearanceCosine)
{
    pfcore::MotionMatcherParams params;
    params.candidateThreshold = 0.0;
    params.similarityThreshold = 0.1;
    params.crossFileGapSec = 0.0;
    params.maxUniqueResults = 10;
    params.requireAppearance = true;
    params.minAppearanceSimilarity = 0.80;
    auto left = window("same", 0.0);
    auto right = window("same", 8.0);
    left.trackId = 84;
    right.trackId = 98;
    left.appearanceEmbedding = {1.0F, 0.0F, 0.0F};
    right.appearanceEmbedding = {0.999F, 0.01F, 0.0F};
    EXPECT_TRUE(pfcore::MotionMatcher(params).findAllPairs({left, right}).empty());
    EXPECT_DOUBLE_EQ(pfcore::MotionMatcher(params).compare(left, right).similarity, 0.0);
}

TEST(MotionMatcher, AllowsReidentifiedTrackAfterSceneCut)
{
    pfcore::MotionMatcherParams params;
    params.candidateThreshold = 0.0;
    params.similarityThreshold = 0.1;
    params.crossFileGapSec = 0.0;
    params.maxUniqueResults = 10;
    params.requireAppearance = true;
    params.minAppearanceSimilarity = 0.80;
    auto left = window("same", 0.0);
    auto right = window("same", 8.0);
    left.trackId = 84;
    right.trackId = 98;
    left.hasSceneIndex = right.hasSceneIndex = true;
    left.sceneIndex = 1;
    right.sceneIndex = 2;
    left.appearanceEmbedding = {1.0F, 0.0F, 0.0F};
    right.appearanceEmbedding = {0.98F, 0.12F, 0.0F};
    left.appearanceConfidence = right.appearanceConfidence = 1.0;
    EXPECT_FALSE(pfcore::MotionMatcher(params).findAllPairs({left, right}).empty());
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
    left.appearanceConfidence = right.appearanceConfidence = 1.0;
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
    left.appearanceConfidence = right.appearanceConfidence = 1.0;
    EXPECT_FALSE(pfcore::MotionMatcher(params).findAllPairs({left, right}).empty());
}

TEST(MotionMatcher, SamePersonAppearanceDoesNotInflateMotionSimilarity)
{
    auto left = window("left", 0.0);
    auto right = window("right", 4.0);
    const double poseOnly = pfcore::MotionMatcher().compare(left, right).similarity;
    left.appearanceEmbedding = {1.0F, 0.0F, 0.0F};
    right.appearanceEmbedding = {0.90F, 0.43F, 0.0F};
    left.appearanceConfidence = right.appearanceConfidence = 1.0;
    const auto withAppearance = pfcore::MotionMatcher().compare(left, right);
    EXPECT_TRUE(withAppearance.appearanceVerified);
    EXPECT_NEAR(withAppearance.similarity, poseOnly, 1e-9);
}

TEST(MotionMatcher, RejectsDifferentMotionFromTheSamePerson)
{
    pfcore::MotionMatcherParams params;
    params.requireAppearance = true;
    params.minAppearanceSimilarity = 0.80;
    const auto left = gestureWindow("left", 0.0, false);
    auto right = gestureWindow("right", 5.0, true);
    auto verifiedLeft = left;
    verifiedLeft.appearanceEmbedding = {1.0F, 0.0F, 0.0F};
    right.appearanceEmbedding = {0.99F, 0.05F, 0.0F};
    verifiedLeft.appearanceConfidence = right.appearanceConfidence = 1.0;
    EXPECT_LT(pfcore::MotionMatcher(params).compare(verifiedLeft, right).similarity,
              params.similarityThreshold);
}

TEST(MotionMatcher, RejectsStaticHeadAndTorsoCropAsAFullPoseMatch)
{
    pfcore::MotionMatcherParams params;
    params.allowStaticFrames = true;
    params.requireAppearance = true;
    auto left = gestureWindow("left", 0.0, false);
    auto right = gestureWindow("right", 5.0, false);
    left.staticFrameSet = right.staticFrameSet = true;
    // Only a face/shoulder fragment is reliable in the right crop.
    for (auto& frame : right.frames) {
        for (std::size_t i = 3; i < frame.keypoints.size(); ++i)
            frame.keypoints[i].confidence = 0.05;
    }
    left.appearanceEmbedding = right.appearanceEmbedding = {1.0F, 0.0F, 0.0F};
    left.appearanceConfidence = right.appearanceConfidence = 1.0;
    EXPECT_DOUBLE_EQ(pfcore::MotionMatcher(params).compare(left, right).similarity, 0.0);
}

TEST(MotionMatcher, RequiredAppearanceRejectsWeakOrMissingEvidence)
{
    pfcore::MotionMatcherParams params;
    params.candidateThreshold = 0.0;
    params.similarityThreshold = 0.1;
    params.crossFileGapSec = 0.0;
    params.maxUniqueResults = 10;
    params.requireAppearance = true;
    auto left = window("left", 0.0);
    auto right = window("right", 4.0);
    left.appearanceEmbedding = right.appearanceEmbedding = {1.0F, 0.0F, 0.0F};
    left.appearanceConfidence = right.appearanceConfidence = 0.25;
    EXPECT_TRUE(pfcore::MotionMatcher(params).findAllPairs({left, right}).empty());
    EXPECT_DOUBLE_EQ(pfcore::MotionMatcher(params).compare(left, right).similarity, 0.0);
    left.appearanceEmbedding.clear();
    right.appearanceEmbedding.clear();
    EXPECT_TRUE(pfcore::MotionMatcher(params).findAllPairs({left, right}).empty());
    EXPECT_DOUBLE_EQ(pfcore::MotionMatcher(params).compare(left, right).similarity, 0.0);

    // Low-level pose-only comparison remains available when explicitly requested.
    params.requireAppearance = false;
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

TEST(MotionMatcher, StaticModeFindsHeldPoseWithoutInventingMotion)
{
    auto left = gestureWindow("a", 0, false);
    for (auto& frame : left.frames) frame.keypoints = left.frames.front().keypoints;
    auto right = left;
    right.sourceId = "b";
    left.staticFrameSet = right.staticFrameSet = true;
    pfcore::MotionMatcherParams params;
    params.allowStaticFrames = true;
    EXPECT_GT(pfcore::MotionMatcher(params).compare(left, right).similarity, 0.8);
    EXPECT_EQ(pfcore::MotionMatcher(params).findAllPairs({left, right}).size(), 1U);
    left.staticFrameSet = right.staticFrameSet = false;
    EXPECT_TRUE(pfcore::MotionMatcher(params).findAllPairs({left, right}).empty());
}

TEST(MotionMatcher, HybridTypesDoNotSuppressEachOtherOrShareResultBudget)
{
    auto motionA = gestureWindow("a", 0, false);
    auto motionB = gestureWindow("b", 8, false);
    auto poseA = motionA, poseB = motionB;
    poseA.staticFrameSet = poseB.staticFrameSet = true;
    for (auto& frame : poseA.frames) frame.keypoints = poseA.frames.front().keypoints;
    for (auto& frame : poseB.frames) frame.keypoints = poseB.frames.front().keypoints;
    const std::vector<pfcore::MotionWindow> windows{motionA, motionB, poseA, poseB};
    pfcore::MotionMatcherParams params;
    params.allowStaticFrames = true;
    params.similarityThreshold = 0.7;
    // Two independent result types each get one slot, even for the same scene pair.
    params.maxUniqueResults = 1;
    const auto matches = pfcore::MotionMatcher(params).findAllPairs(windows);
    ASSERT_EQ(matches.size(), 2u);
    EXPECT_NE(windows[matches[0].leftIndex].staticFrameSet,
              windows[matches[1].leftIndex].staticFrameSet);
}

TEST(MotionMatcher, ReversedSourcePairsAreTheSameParallel)
{
    auto a = gestureWindow("a", 0, false);
    auto b = gestureWindow("b", 8, false);
    pfcore::MotionMatcherParams params;
    params.maxUniqueResults = 100;
    const auto matches = pfcore::MotionMatcher(params).findAllPairs({a, b, b, a});
    EXPECT_EQ(matches.size(), 1u);
}

TEST(MotionMatcher, ContextualDuplicateSuppressionIsOrientationIndependent)
{
    auto a = gestureWindow("a", 0, false);
    auto b = gestureWindow("b", 8, false);
    auto nextA = gestureWindow("a", 2, false);
    auto nextB = gestureWindow("b", 10, false);
    for (auto* item : {&a, &b, &nextA, &nextB}) item->sceneContext = {1.0F};
    pfcore::MotionMatcherParams params;
    params.maxUniqueResults = 100;
    const pfcore::MotionMatcher matcher(params);
    const auto direct = matcher.findAllPairs({a, b, nextA, nextB});
    const auto reversed = matcher.findAllPairs({a, b, nextB, nextA});
    ASSERT_FALSE(direct.empty());
    EXPECT_EQ(reversed.size(), direct.size());
}

TEST(MotionMatcher, SameScenePairKeepsDifferentVerifiedPeople)
{
    std::vector<pfcore::MotionWindow> windows;
    for (int person = 0; person < 2; ++person) {
        for (int side = 0; side < 2; ++side) {
            auto item = gestureWindow(side == 0 ? "source-a" : "source-b", side * 8.0, false);
            item.hasSceneIndex = true;
            item.sceneIndex = side + 1;
            item.trackId = static_cast<std::size_t>(person + 1);
            item.faceEmbedding = person == 0
                ? std::vector<float>{1.0F, 0.0F} : std::vector<float>{0.0F, 1.0F};
            item.faceConfidence = 1.0;
            windows.push_back(std::move(item));
        }
    }
    pfcore::MotionMatcherParams params;
    params.requireAppearance = true;
    const auto matches = pfcore::MotionMatcher(params).findAllPairs(windows);
    ASSERT_EQ(matches.size(), 2u);
    for (const auto& match : matches) {
        EXPECT_EQ(windows[match.leftIndex].trackId, windows[match.rightIndex].trackId);
    }
}

TEST(MotionMatcher, Retains120IndependentVerifiedParallels)
{
    std::vector<pfcore::MotionWindow> windows;
    for (int identity = 0; identity < 120; ++identity) {
        for (int side = 0; side < 2; ++side) {
            auto item = gestureWindow("source", side * 8.0, false);
            item.sourceId = "source_" + std::to_string(identity) + "_" + std::to_string(side);
            item.appearanceEmbedding.assign(120, 0.0F);
            item.appearanceEmbedding[identity] = 1.0F;
            item.appearanceConfidence = 1.0;
            windows.push_back(std::move(item));
        }
    }
    pfcore::MotionMatcherParams params;
    params.requireAppearance = true;
    params.maxUniqueResults = 150;
    const auto matches = pfcore::MotionMatcher(params).findAllPairs(windows);
    ASSERT_EQ(matches.size(), 120u);
    for (const auto& match : matches) {
        EXPECT_EQ(match.leftIndex / 2, match.rightIndex / 2);
        EXPECT_TRUE(match.appearanceVerified);
    }
}

TEST(MotionMatcher, RepeatedGestureSurvivesIsolatedMissingObservations)
{
    auto left = gestureWindow("a", 0, false);
    auto right = gestureWindow("b", 8, false);
    // A detector dropout every fifth observation must not break the whole
    // gesture into unusable runs; missing data is never a matching frame.
    for (std::size_t i = 4; i < right.frames.size(); i += 5)
        for (auto& joint : right.frames[i].keypoints) joint.confidence = 0;
    pfcore::MotionMatcher matcher;
    EXPECT_GT(matcher.compare(left, right).similarity, 0.78);
    EXPECT_NEAR(matcher.compare(left, right).similarity,
                matcher.compare(right, left).similarity, 1e-9);
}

TEST(MotionMatcher, ShortGestureSupportsSparseSampling)
{
    auto left = window("a", 0, false, 8, 0.14);
    auto right = window("b", 8, false, 8, 0.14);
    EXPECT_GT(pfcore::MotionMatcher().compare(left, right).similarity, 0.78);
    EXPECT_EQ(pfcore::MotionMatcher().findAllPairs({left, right}).size(), 1U);
}

TEST(MotionMatcher, AdjacentVerifiedShotsAreNotBlockedBySameSourceTimeGap)
{
    auto left = window("scenepack", 2.0);
    auto right = window("scenepack", 3.0);
    left.hasSceneIndex = right.hasSceneIndex = true;
    left.sceneIndex = 1; right.sceneIndex = 2;
    left.sceneStartSeconds = 2.0; left.sceneEndSeconds = 3.0;
    right.sceneStartSeconds = 3.0; right.sceneEndSeconds = 4.0;
    left.appearanceEmbedding = right.appearanceEmbedding = {1.0F, 0.0F};
    left.appearanceConfidence = right.appearanceConfidence = 1.0;
    pfcore::MotionMatcherParams params;
    params.requireAppearance = true;
    const pfcore::MotionMatcher matcher(params);
    EXPECT_GT(matcher.compare(left, right).similarity, 0.78);
    EXPECT_EQ(matcher.findAllPairs({left, right}).size(), 1U);

    // Different labels alone are not enough: the shot intervals must not
    // overlap, and missing identity evidence never makes a valid parallel.
    right.sceneStartSeconds = 2.5;
    EXPECT_TRUE(matcher.findAllPairs({left, right}).empty());
    right.sceneStartSeconds = 3.0;
    right.appearanceEmbedding.clear();
    EXPECT_TRUE(matcher.findAllPairs({left, right}).empty());
}

TEST(MotionMatcher, ThreeAdjacentShotsRetainThreeDifferentScenePairs)
{
    std::vector<pfcore::MotionWindow> shots;
    for (int i = 0; i < 3; ++i) {
        auto shot = window("scenepack", 2.0 + i);
        shot.hasSceneIndex = true;
        shot.sceneIndex = i;
        shot.sceneStartSeconds = 2.0 + i;
        shot.sceneEndSeconds = 3.0 + i;
        shot.appearanceEmbedding = {1.0F, 0.0F};
        shot.appearanceConfidence = 1.0;
        shots.push_back(shot);
    }
    pfcore::MotionMatcherParams params;
    params.requireAppearance = true;
    const auto pairs = pfcore::MotionMatcher(params).findAllPairs(shots);
    EXPECT_EQ(pairs.size(), 3U); // A/B, A/C, B/C, not three sliding copies.
}

TEST(MotionMatcher, OneShotCannotBecomeAnUnlimitedResultHub)
{
    std::vector<pfcore::MotionWindow> shots;
    for (std::size_t i = 0; i < 10; ++i) {
        auto shot = window("long-video", 10.0 * i);
        shot.hasSceneIndex = true;
        shot.sceneIndex = i;
        shot.sceneStartSeconds = 10.0 * i;
        shot.sceneEndSeconds = 10.0 * i + 1.0;
        shot.appearanceEmbedding = {1.0F, 0.0F};
        shot.appearanceConfidence = 1.0;
        shots.push_back(shot);
    }
    pfcore::MotionMatcherParams params;
    params.requireAppearance = true;
    const auto pairs = pfcore::MotionMatcher(params).findAllPairs(shots);
    ASSERT_FALSE(pairs.empty());
    std::vector<std::size_t> degree(shots.size());
    for (const auto& pair : pairs) { ++degree[pair.leftIndex]; ++degree[pair.rightIndex]; }
    EXPECT_LE(*std::max_element(degree.begin(), degree.end()), 3U);
    EXPECT_GE(std::count_if(degree.begin(), degree.end(), [](auto n) { return n > 0; }), 8);
    params.maxResultsPerShot = 0;
    EXPECT_GT(pfcore::MotionMatcher(params).findAllPairs(shots).size(), pairs.size());
}

TEST(MotionMatcher, RepeatedCameraPoseDoesNotFillTheStaticBudget)
{
    auto a = deanAimingPoseFixture()[0], b = a;
    a.sourceId = b.sourceId = "conversation";
    a.sceneContext = b.sceneContext = {1.0F, 0.0F};
    for (auto& f : b.frames) f.timestampSeconds += 60.0;
    pfcore::MotionMatcherParams params;
    params.allowStaticFrames = true;
    ASSERT_GE(pfcore::MotionMatcher(params).compare(a,b).similarity, params.similarityThreshold);
    EXPECT_TRUE(pfcore::MotionMatcher(params).findAllPairs({a,b}).empty());
    b.sceneContext = {0.0F, 1.0F};
    EXPECT_EQ(pfcore::MotionMatcher(params).findAllPairs({a,b}).size(), 1U);
    b.sceneContext = a.sceneContext;
    for (auto& f : b.frames) f.timestampSeconds += 240.0;
    EXPECT_EQ(pfcore::MotionMatcher(params).findAllPairs({a,b}).size(), 1U);
}

TEST(MotionMatcher, MaskedBodyEvidenceCannotOverrideKnownDifferentFaces)
{
    auto a = deanAimingPoseFixture()[0], b = a;
    a.sourceId = "masked-shot-a"; b.sourceId = "masked-shot-b";
    a.faceEmbedding.clear(); b.faceEmbedding.clear();
    a.appearanceEmbedding = b.appearanceEmbedding = {1.0F, 0.0F};
    a.appearanceConfidence = b.appearanceConfidence = 1.0;
    pfcore::MotionMatcherParams params;
    params.allowStaticFrames = true;
    params.requireAppearance = true;
    ASSERT_GE(pfcore::MotionMatcher(params).compare(a,b).similarity, params.similarityThreshold);
    b.appearanceEmbedding = {0.0F, 1.0F};
    EXPECT_DOUBLE_EQ(pfcore::MotionMatcher(params).compare(a,b).similarity, 0.0);
    b.appearanceEmbedding = a.appearanceEmbedding;
    a.faceEmbedding = {1.0F, 0.0F}; b.faceEmbedding = {0.0F, 1.0F};
    a.faceConfidence = b.faceConfidence = 1.0;
    EXPECT_DOUBLE_EQ(pfcore::MotionMatcher(params).compare(a,b).similarity, 0.0);
    a.faceEmbedding.clear(); b.faceEmbedding.clear();
    a.appearanceConfidence = b.appearanceConfidence = 0.1;
    EXPECT_DOUBLE_EQ(pfcore::MotionMatcher(params).compare(a,b).similarity, 0.0);
}

TEST(MotionMatcher, HiddenFaceNeedsSixObservedLimbJointsForStaticSupport)
{
    auto body = deanFoldedArmsPoseFixture()[0];
    for (auto& f : body.frames)
        for (std::size_t j = 0; j < 5; ++j) f.keypoints[j].confidence = 0.0;
    EXPECT_TRUE(pfcore::observedPoseRuns(body).empty());
    ASSERT_FALSE(pfcore::observedPoseRuns(body, true).empty());
    auto repeat = body;
    repeat.sourceId = "masked-repeat";
    pfcore::MotionMatcherParams params;
    params.allowStaticFrames = true;
    params.requireAppearance = true;
    params.requireObservedBodyForIdentity = true;
    EXPECT_GE(pfcore::MotionMatcher(params).compare(body,repeat).similarity, params.similarityThreshold);
    for (auto& f : body.frames) f.keypoints[9].confidence = 0.0;
    EXPECT_TRUE(pfcore::observedPoseRuns(body, true).empty());
}

TEST(MotionMatcher, HiddenFaceArticulationDoesNotConfuseFoldedArmsWithAiming)
{
    auto folded = deanFoldedArmsPoseFixture()[0];
    auto aiming = deanAimingPoseFixture()[0];
    for (auto* shot : {&folded, &aiming}) {
        shot->faceEmbedding.clear();
        for (auto& f : shot->frames)
            for (std::size_t j = 0; j < 5; ++j) f.keypoints[j].confidence = 0.0;
    }
    pfcore::MotionMatcherParams params;
    params.allowStaticFrames = true;
    params.requireAppearance = true;
    params.requireObservedBodyForIdentity = true;
    params.staticArticulationSimilarityThreshold = 0.82;
    EXPECT_DOUBLE_EQ(pfcore::MotionMatcher(params).compare(folded, aiming).similarity, 0.0);
    auto otherPerson = folded;
    otherPerson.sourceId = "other-masked-person";
    otherPerson.appearanceEmbedding = {0.0F, 1.0F};
    EXPECT_DOUBLE_EQ(pfcore::MotionMatcher(params).compare(folded, otherPerson).similarity, 0.0);
}

TEST(MotionMatcher, BodyIdentityCannotValidateAnUnobservedMaskedGesture)
{
    auto a = deanAimingPoseFixture()[0], b = a;
    a.sourceId = "mask-a"; b.sourceId = "mask-b";
    for (auto* shot : {&a, &b}) {
        shot->faceEmbedding.clear();
        shot->appearanceEmbedding = {1.0F, 0.0F};
        shot->appearanceConfidence = 1.0;
        for (auto& frame : shot->frames)
            for (std::size_t joint = 7; joint < frame.keypoints.size(); ++joint)
                frame.keypoints[joint].confidence = 0.0;
    }
    pfcore::MotionMatcherParams params;
    params.allowStaticFrames = true;
    params.requireAppearance = true;
    params.requireObservedBodyForIdentity = true;
    EXPECT_DOUBLE_EQ(pfcore::MotionMatcher(params).compare(a,b).similarity, 0.0);
}

TEST(MotionMatcher, ShortStaticShotHasIndependentTemporalSupport)
{
    auto left = window("a", 0, false, 3, 1.0 / 6.0);
    auto right = left;
    right.sourceId = "b";
    left.staticFrameSet = right.staticFrameSet = true;
    // Held pose in three independently timestamped observations, not a
    // claim that an entire movement is established by three stills.
    for (auto* shot : {&left, &right}) {
        for (auto& frame : shot->frames) frame.keypoints = {{0, 0}, {1, 1}};
        shot->appearanceEmbedding = {1.0F, 0.0F};
        shot->appearanceConfidence = 1.0;
    }
    pfcore::MotionMatcherParams params;
    params.allowStaticFrames = true;
    params.requireAppearance = true;
    const pfcore::MotionMatcher matcher(params);
    EXPECT_GT(matcher.compare(left, right).similarity, 0.85);
    EXPECT_EQ(matcher.findAllPairs({left, right}).size(), 1U);
    right.frames.pop_back();
    EXPECT_DOUBLE_EQ(matcher.compare(left, right).similarity, 0.0);
    right = left; right.sourceId = "b";
    left.staticFrameSet = right.staticFrameSet = false;
    EXPECT_TRUE(matcher.findAllPairs({left, right}).empty());
}

TEST(MotionMatcher, SameSourceOverlappingWindowsCannotBeParallels)
{
    auto left = window("a", 0, false, 100, 0.1);
    auto right = window("a", 6, false, 100, 0.1);
    EXPECT_TRUE(pfcore::MotionMatcher().findAllPairs({left, right}).empty());
}

TEST(MotionMatcher, RepeatedGestureAtDifferentSamplingRates)
{
    auto left = gestureWindow("a", 0, false);
    auto right = left;
    right.sourceId = "b";
    right.frames.clear();
    for (std::size_t i = 0; i < left.frames.size(); i += 2) {
        auto frame = left.frames[i];
        frame.timestampSeconds = 8 + frame.timestampSeconds;
        right.frames.push_back(frame);
    }
    pfcore::MotionMatcher matcher;
    EXPECT_GT(matcher.compare(left, right).similarity, 0.78);
    EXPECT_NEAR(matcher.compare(left, right).similarity,
                matcher.compare(right, left).similarity, 1e-9);
}

TEST(MotionMatcher, RepeatedGestureAtDifferentTempo)
{
    auto left = gestureWindow("a", 0, false);
    auto right = left;
    right.sourceId = "b";
    for (auto& frame : right.frames) frame.timestampSeconds = 8 + frame.timestampSeconds * 1.5;
    EXPECT_GT(pfcore::MotionMatcher().compare(left, right).similarity, 0.78);
}

TEST(MotionMatcher, MissingCocoJointsDoNotPoisonCandidateIndex)
{
    pfcore::MotionWindow left;
    left.sourceId = "a";
    for (int i = 0; i < 24; ++i) {
        pfcore::PoseFrame frame;
        frame.timestampSeconds = i / 12.0;
        for (int joint = 0; joint < 17; ++joint) {
            frame.keypoints.push_back({0.1 * (joint % 3), 0.1 * (joint / 3), 1.0});
        }
        frame.keypoints[9].x += 0.5 * std::sin(i * 0.18);
        frame.keypoints[10].y += 0.5 * std::sin(i * 0.18);
        frame.keypoints[15].confidence = frame.keypoints[16].confidence = 0;
        left.frames.push_back(frame);
    }
    auto right = left;
    right.sourceId = "b";
    const auto matches = pfcore::MotionMatcher().findAllPairs({left, right});
    ASSERT_EQ(matches.size(), 1U);
    EXPECT_GT(matches.front().similarity, 0.78);
    for (const auto& match : matches) EXPECT_TRUE(std::isfinite(match.similarity));
}

TEST(MotionMatcher, SharedCameraDriftDoesNotMatchDifferentActiveBodyParts)
{
    pfcore::MotionWindow arm, leg;
    arm.sourceId = "arm"; leg.sourceId = "leg";
    for (int i = 0; i < 24; ++i) {
        const double t = i / 12.0;
        pfcore::PoseFrame base;
        base.timestampSeconds = t;
        for (int joint = 0; joint < 17; ++joint)
            base.keypoints.push_back({0.2 * (joint % 3) + 0.12 * t, 0.2 * (joint / 3), 1.0});
        auto hand = base;
        auto foot = base;
        hand.keypoints[9].y -= 0.15 * t;
        hand.keypoints[10].y -= 0.15 * t;
        foot.keypoints[15].y -= 0.15 * t;
        foot.keypoints[16].y -= 0.15 * t;
        arm.frames.push_back(hand);
        leg.frames.push_back(foot);
    }
    const pfcore::MotionMatcher matcher;
    auto repeatedArm = arm;
    repeatedArm.sourceId = "repeated-arm";
    EXPECT_GT(matcher.compare(arm, repeatedArm).similarity, 0.78);
    EXPECT_LT(matcher.compare(arm, leg).similarity, 0.70);
}

TEST(MotionMatcher, SharedCameraDriftCannotHideOppositeHeadMotion)
{
    pfcore::MotionWindow left, opposite, locomotion;
    left.sourceId = "head-left";
    opposite.sourceId = "head-right";
    locomotion.sourceId = "rigid-translation";
    for (int i = 0; i < 24; ++i) {
        const double t = i / 12.0;
        pfcore::PoseFrame base;
        base.timestampSeconds = t;
        for (int joint = 0; joint < 17; ++joint)
            base.keypoints.push_back({0.2 * (joint % 3) + 0.5 * t,
                                      0.2 * (joint / 3), 1.0});
        auto a = base, b = base;
        for (int joint = 0; joint < 5; ++joint) {
            a.keypoints[joint].x += 0.10 * t;
            b.keypoints[joint].x -= 0.10 * t;
        }
        left.frames.push_back(a);
        opposite.frames.push_back(b);
        locomotion.frames.push_back(base);
    }
    const pfcore::MotionMatcher matcher;
    auto repeat = left;
    repeat.sourceId = "head-repeat";
    EXPECT_GT(matcher.compare(left, repeat).similarity, 0.78);
    EXPECT_LT(matcher.compare(left, opposite).similarity, 0.70);
    for (auto& frame : repeat.frames) frame.timestampSeconds *= 1.5;
    EXPECT_GT(matcher.compare(left, repeat).similarity, 0.78);
    auto repeatedLocomotion = locomotion;
    repeatedLocomotion.sourceId = "rigid-repeat";
    EXPECT_GT(matcher.compare(locomotion, repeatedLocomotion).similarity, 0.78);
}

TEST(MotionMatcher, PreviewStartsAtSupportedGesture)
{
    auto left = gestureWindow("a", 0, false);
    auto right = gestureWindow("b", 8, false);
    for (std::size_t i = 0; i < 5; ++i)
        for (auto& joint : right.frames[i].keypoints) joint.confidence = 0;
    const auto match = pfcore::MotionMatcher().compare(left, right);
    EXPECT_GT(match.similarity, 0.78);
    EXPECT_GE(match.rightStartSeconds, right.frames[5].timestampSeconds);
    EXPECT_LE(match.rightEndSeconds, right.frames.back().timestampSeconds);
}

} // namespace
