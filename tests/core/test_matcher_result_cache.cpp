#include <gtest/gtest.h>
#include <pfservices/MatcherResultCache.hpp>
#include <pfservices/PfCache.hpp>
#include <QTemporaryDir>

namespace {
std::vector<pfcore::MotionWindow> inputs()
{
    std::vector<pfcore::MotionWindow> windows(2);
    windows[0].sourceId="first";windows[1].sourceId="second";
    for(auto& window:windows) {
        window.frames={pfcore::PoseFrame{1,{{1,2,.9}}},pfcore::PoseFrame{2,{{3,4,.8}}}};
        window.sceneStartSeconds=1;window.sceneEndSeconds=2;
    }
    return windows;
}
pfcore::MotionMatch rawMatch()
{
    pfcore::MotionMatch match;
    match.rightIndex=1;match.leftSourceId="first";match.rightSourceId="second";
    match.similarity=.91;match.durationSeconds=1;match.dtwDistance=.123;
    match.leftStartSeconds=match.rightStartSeconds=1;
    match.leftEndSeconds=match.rightEndSeconds=2;
    match.appearanceVerified=true;match.headOnlyComparison=true;
    match.unmirroredSimilarity=.87;return match;
}
}

TEST(MatcherResultCache, ExactRoundTripIncludingEmptyResultsAndDiskRestart)
{
    auto windows=inputs();const std::vector matches{rawMatch()};
    const auto bytes=pfservices::serializeMatcherResults(matches);ASSERT_FALSE(bytes.empty());
    std::vector<pfcore::MotionMatch> restored;
    ASSERT_TRUE(pfservices::deserializeMatcherResults(bytes,windows,restored));
    ASSERT_EQ(restored.size(),1U);
    EXPECT_EQ(restored[0].similarity,matches[0].similarity);
    EXPECT_EQ(restored[0].dtwDistance,matches[0].dtwDistance);
    EXPECT_EQ(restored[0].headOnlyComparison,matches[0].headOnlyComparison);
    EXPECT_EQ(restored[0].unmirroredSimilarity,matches[0].unmirroredSimilarity);
    EXPECT_EQ(pfservices::serializeMatcherResults(restored),bytes);
    QTemporaryDir directory;ASSERT_TRUE(directory.isValid());std::string error;
    const auto hash=pfcore::motionSearchInputFingerprint(windows,{});ASSERT_TRUE(hash);
    const auto key=pfservices::matcherResultCacheKey(*hash,"model/source/generation");
    {pfservices::PfCache first(directory.path().toStdWString());ASSERT_TRUE(first.put(key,bytes,error));}
    pfservices::PfCache restarted(directory.path().toStdWString());
    ASSERT_EQ(restarted.get(key),std::optional(bytes));
    EXPECT_NE(key,pfservices::matcherResultCacheKey(*hash,"changed-model/source/generation"));
    const auto empty=pfservices::serializeMatcherResults({});ASSERT_FALSE(empty.empty());
    ASSERT_TRUE(pfservices::deserializeMatcherResults(empty,windows,restored));EXPECT_TRUE(restored.empty());
}

TEST(MatcherResultCache, CorruptionAndWrongInputCannotPublishPartialResults)
{
    auto windows=inputs();const std::vector matches{rawMatch()};
    auto bytes=pfservices::serializeMatcherResults(matches);bytes[20]^=1;
    auto restored=matches;
    EXPECT_FALSE(pfservices::deserializeMatcherResults(bytes,windows,restored));
    ASSERT_EQ(restored.size(),1U);EXPECT_EQ(restored[0].similarity,.91);
    bytes=pfservices::serializeMatcherResults(matches);windows[0].sourceId="wrong-source";
    EXPECT_FALSE(pfservices::deserializeMatcherResults(bytes,windows,restored));
    windows=inputs();windows[1].frames.back().timestampSeconds=1.5;
    EXPECT_FALSE(pfservices::deserializeMatcherResults(bytes,windows,restored));
    EXPECT_FALSE(pfservices::deserializeMatcherResults(std::span(bytes).first(10),inputs(),restored));
}

TEST(MatcherResultCache, FingerprintSeparatesMatchingFromResultOnlyClassification)
{
    auto windows=inputs();const pfcore::MotionMatcherParams params;
    const auto fingerprint=pfcore::motionSearchInputFingerprint(windows,params);ASSERT_TRUE(fingerprint);
    windows[0].headOrientations.push_back({});windows[0].bodyOrientations.push_back({});
    windows[0].videoActions.push_back({});
    EXPECT_EQ(pfcore::motionSearchInputFingerprint(windows,params),fingerprint);
    windows[0].measuredFaces.push_back({1,true,.2,.1});
    EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,params),fingerprint);
    windows=inputs();windows[0].frames[0].keypoints[0].x+=.01;
    EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,params),fingerprint);
    windows=inputs();std::swap(windows[0],windows[1]);
    EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,params),fingerprint);
    EXPECT_FALSE(pfcore::motionSearchInputFingerprint(windows,params,[]{return true;}));
}

TEST(MatcherResultCache, EverySearchParameterParticipatesInPersistentIdentity)
{
    const auto windows=inputs();const pfcore::MotionMatcherParams original;
    const auto expected=pfcore::motionSearchInputFingerprint(windows,original);ASSERT_TRUE(expected);
    {auto changed=original;changed.auditDirectFaceConsensus=!changed.auditDirectFaceConsensus;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"auditDirectFaceConsensus";}
    {auto changed=original;changed.similarityThreshold+=1;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"similarityThreshold";}
    {auto changed=original;changed.candidateThreshold+=1;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"candidateThreshold";}
    {auto changed=original;changed.minRepeatGapSec+=1;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"minRepeatGapSec";}
    {auto changed=original;changed.sameFileGapSec+=1;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"sameFileGapSec";}
    {auto changed=original;changed.crossFileGapSec+=1;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"crossFileGapSec";}
    {auto changed=original;changed.duplicateWindowSec+=1;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"duplicateWindowSec";}
    {auto changed=original;changed.noiseFactor+=1;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"noiseFactor";}
    {auto changed=original;changed.maxUniqueResults+=1;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"maxUniqueResults";}
    {auto changed=original;changed.maxResultsPerShot+=1;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"maxResultsPerShot";}
    {auto changed=original;changed.individualPairs=!changed.individualPairs;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"individualPairs";}
    {auto changed=original;changed.recoverUnusedShots=!changed.recoverUnusedShots;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"recoverUnusedShots";}
    {auto changed=original;changed.coverageSeedLimit+=1;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"coverageSeedLimit";}
    {auto changed=original;changed.bodyMotionOnly=!changed.bodyMotionOnly;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"bodyMotionOnly";}
    {auto changed=original;changed.maxComparisonThreads+=1;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"maxComparisonThreads";}
    {auto changed=original;changed.reuseCameraConflicts=!changed.reuseCameraConflicts;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"reuseCameraConflicts";}
    {auto changed=original;changed.reuseRejectedComparisons=!changed.reuseRejectedComparisons;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"reuseRejectedComparisons";}
    {auto changed=original;changed.expandedSearch=!changed.expandedSearch;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"expandedSearch";}
    {auto changed=original;changed.timeWeight+=1;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"timeWeight";}
    {auto changed=original;changed.normalizeSize=!changed.normalizeSize;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"normalizeSize";}
    {auto changed=original;changed.mirrorInvariant=!changed.mirrorInvariant;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"mirrorInvariant";}
    {auto changed=original;changed.dtwBand+=1;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"dtwBand";}
    {auto changed=original;changed.sakoeChibaRatio+=1;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"sakoeChibaRatio";}
    {auto changed=original;changed.motionDeltaThreshold+=1;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"motionDeltaThreshold";}
    {auto changed=original;changed.minActiveTransitionRatio+=1;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"minActiveTransitionRatio";}
    {auto changed=original;changed.minMotionRange+=1;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"minMotionRange";}
    {auto changed=original;changed.minMotionSpanSec+=1;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"minMotionSpanSec";}
    {auto changed=original;changed.temporalSimilarityThreshold+=1;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"temporalSimilarityThreshold";}
    {auto changed=original;changed.staticPoseSimilarityThreshold+=1;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"staticPoseSimilarityThreshold";}
    {auto changed=original;changed.staticArticulationSimilarityThreshold+=1;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"staticArticulationSimilarityThreshold";}
    {auto changed=original;changed.bodyScaleInvariant=!changed.bodyScaleInvariant;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"bodyScaleInvariant";}
    {auto changed=original;changed.minTemporalFrames+=1;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"minTemporalFrames";}
    {auto changed=original;changed.minTemporalDurationSec+=1;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"minTemporalDurationSec";}
    {auto changed=original;changed.minimumResultDurationSec+=1;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"minimumResultDurationSec";}
    {auto changed=original;changed.sameSourceGapFloorSec+=1;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"sameSourceGapFloorSec";}
    {auto changed=original;changed.nmsOverlapThreshold+=1;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"nmsOverlapThreshold";}
    {auto changed=original;changed.requireSameTrackWithinSource=!changed.requireSameTrackWithinSource;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"requireSameTrackWithinSource";}
    {auto changed=original;changed.allowStaticFrames=!changed.allowStaticFrames;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"allowStaticFrames";}
    {auto changed=original;changed.requireAppearance=!changed.requireAppearance;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"requireAppearance";}
    {auto changed=original;changed.minAppearanceSimilarity+=1;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"minAppearanceSimilarity";}
    {auto changed=original;changed.minMotionBodyIdentitySimilarity+=1;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"minMotionBodyIdentitySimilarity";}
    {auto changed=original;changed.appearanceWeight+=1;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"appearanceWeight";}
    {auto changed=original;changed.minAppearanceEvidence+=1;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"minAppearanceEvidence";}
    {auto changed=original;changed.minFaceSimilarity+=1;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"minFaceSimilarity";}
    {auto changed=original;changed.minHeadFaceSimilarity+=1;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"minHeadFaceSimilarity";}
    {auto changed=original;changed.sameSceneContextThreshold+=1;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"sameSceneContextThreshold";}
    {auto changed=original;changed.sameSceneContextGapSec+=1;EXPECT_NE(pfcore::motionSearchInputFingerprint(windows,changed),expected)<<"sameSceneContextGapSec";}
}
