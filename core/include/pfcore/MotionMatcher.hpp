#pragma once

#include <cstddef>
#include <string>
#include <vector>
#include <stop_token>

namespace pfcore {

struct Keypoint {
    double x = 0.0;
    double y = 0.0;
    double confidence = 1.0;
};

struct PoseFrame {
    double timestampSeconds = 0.0;
    std::vector<Keypoint> keypoints;
};

struct MotionWindow {
    std::string sourceId;
    // Stable provenance carried from the detector. Invalid values are used
    // by callers that construct synthetic windows and mean "unknown".
    std::size_t trackId = 0;
    std::size_t sceneIndex = 0;
    bool hasSceneIndex = false;
    // Static-frame analysis still uses a temporal set of independent samples;
    // this flag tells the matcher not to require motion, while preserving the
    // multi-frame/temporal coverage guard.
    bool staticFrameSet = false;
    // Full shot bounds retained separately from the shorter motion chunk.
    // They constrain the short context used for playback and export.
    double sceneStartSeconds = -1.0;
    double sceneEndSeconds = -1.0;
    std::vector<PoseFrame> frames;
    // L2-normalized body-ReID prototype for this temporal window. Empty means
    // the optional ReID model was unavailable and the result is pose-only.
    std::vector<float> appearanceEmbedding;
    double appearanceConfidence = 0.0;
    std::vector<float> faceEmbedding;
    double faceConfidence = 0.0;
    // Recomputed from thumbnails even on a cache hit; not an identity vector.
    std::vector<float> sceneContext;
};

struct MotionMatcherParams {
    // A short held pose needs repeated observations, not the longer trajectory
    // required to establish motion. Shared with scene-window extraction.
    static constexpr std::size_t minimumStaticSamples = 3;
    static constexpr double minimumStaticSpanSeconds = 0.30;
    double similarityThreshold = 0.78;
    double candidateThreshold = 0.50;
    double minRepeatGapSec = 6.0;
    double sameFileGapSec = 2.0;
    double crossFileGapSec = 0.0;
    double duplicateWindowSec = 1.5;
    double noiseFactor = 1.0;
    // Per result type: hybrid mode has independent motion and static budgets.
    // Within static, accepted head/body comparisons share this same budget
    // fairly; unused capacity is lent to the other observable region.
    std::size_t maxUniqueResults = 100;
    // Independent result types have separate budgets/duplicate metrics, but
    // the per-shot reuse quota below is shared across both types.
    // Default: one result per shot. Callers can explicitly opt into reuse.
    // Zero disables this diversity quota for diagnostic comparisons.
    std::size_t maxResultsPerShot = 1;
    // Zero: up to four workers on large exact-comparison sets. One forces
    // serial execution for reproducible A/B checks. Retrieval and selection
    // stay serial; no candidate or sample is dropped by this setting.
    std::size_t maxComparisonThreads = 0;
    // Wider deterministic retrieval; the desktop also derives short supported
    // pose alternatives from existing observations. Acceptance stays intact.
    bool expandedSearch = false;
    double timeWeight = 0.25;
    bool normalizeSize = true;
    // Compare both the original trajectory and a left/right mirrored copy,
    // keeping the stronger motion match. Mirroring stays inside the matcher
    // so detector/tracker geometry and identity embeddings remain untouched.
    bool mirrorInvariant = false;
    std::size_t dtwBand = 2;
    // Relative Sakoe–Chiba width. dtwBand remains a useful lower bound for
    // short windows, while this ratio keeps the constraint proportional to a
    // longer sampled trajectory.
    double sakoeChibaRatio = 0.10;

    // A window is a movement only when the normalized mean joint delta is
    // above this value.  The value is intentionally independent of the
    // number of keypoints, so models with a different skeleton size behave
    // consistently.
    // This remains above ordinary detector jitter while allowing a genuine
    // short gesture to survive at 24/30 FPS.
    // Pose detectors often emit 1–2 px of legitimate movement at 24/30 FPS.
    // Keep the static gate, but do not make a real short gesture disappear.
    double motionDeltaThreshold = 0.008;
    // A small active-transition ratio is allowed because a window can enter
    // from a neutral pose; trajectory range and temporal-run checks below are
    // the stronger guards against a one-frame/noise candidate.
    double minActiveTransitionRatio = 0.03;
    // Mean range of a joint trajectory across the window. It filters pose
    // jitter that happens to exceed one transition threshold.
    double minMotionRange = 0.06;
    // Minimum span of a supported continuous run. It prevents one-frame
    // candidates even when a clip was sampled sparsely.
    double minMotionSpanSec = 0.75;
    // DTW still supplies the global score, but a valid match must also contain
    // a contiguous run of similar frames.  Short clips may satisfy the
    // duration alternative when the selected quality profile samples fewer
    // than 18 pose frames per second.
    // Frame similarity is calibrated from pose and velocity. The threshold is
    // deliberately lower than the final score because camera angle and crop
    // change the 2D skeleton; duration, DTW, anatomy and the final threshold
    // still have to agree before a pair is published.
    double temporalSimilarityThreshold = 0.42;
    // Static-pose mode is stricter: it has no motion trajectory to help
    // disambiguate the same person standing in unrelated scenes.
    double staticPoseSimilarityThreshold = 0.86;
    // Independently verified, sustained limb articulation tolerates view
    // changes separately from generic pose geometry and head-only matches.
    double staticArticulationSimilarityThreshold = 0.86;
    // At least ten observations are sampled for a window, but a valid
    // alignment may be a shorter, six-frame gesture. Half a second is long
    // enough to rule out a copied still frame while retaining real edits
    // whose repeated action only lasts one beat.
    std::size_t minTemporalFrames = 10;
    double minTemporalDurationSec = 0.50;
      // Same-source windows cannot match inside this floor, including verified
      // identities in disjoint shots. Callers may explicitly opt out with zero.
    double sameSourceGapFloorSec = 5.0;
    double nmsOverlapThreshold = 0.35;
    // Track IDs are local to a source file.  A different ID must not silently
    // become the same person: within one source it is allowed only when the
    // body-ReID gate independently verifies the appearance. Cross-file IDs
    // are not comparable and are therefore intentionally ignored.
    bool requireSameTrackWithinSource = true;
    bool allowStaticFrames = false;
    bool requireAppearance = false;
    // Required appearance fails closed on absent/weak identity evidence.
    double minAppearanceSimilarity = 0.80;
    // Kept for settings compatibility. Appearance is an identity gate only;
    // it deliberately never inflates the user-visible motion similarity.
    double appearanceWeight = 0.0;
    // Confidence from independent valid ReID crops in a temporal window.
    // It is not calculated per pose frame because ReID is sampled less often.
    // A single lucky/blurred crop must not establish an identity.
    double minAppearanceEvidence = 0.45;
    double minFaceSimilarity = 0.363;
    // Facial geometry alone supplies much less independent pose evidence
    // than an observed body gesture. Require stronger identity confirmation
    // for a head-only result; never apply this to a visible limb comparison.
    double minHeadFaceSimilarity = 0.70;
    double sameSceneContextThreshold = 0.90;
    double sameSceneContextGapSec = 30.0;
};

struct MotionMatch {
    std::size_t leftIndex = 0;
    std::size_t rightIndex = 0;
    std::string leftSourceId;
    std::string rightSourceId;
    double similarity = 0.0;
    double dtwDistance = 0.0;
    double durationSeconds = 0.0;
    double leftStartSeconds = 0.0;
    double leftEndSeconds = 0.0;
    double rightStartSeconds = 0.0;
    double rightEndSeconds = 0.0;
    double leftSceneStartSeconds = 0.0;
    double leftSceneEndSeconds = 0.0;
    double rightSceneStartSeconds = 0.0;
    double rightSceneEndSeconds = 0.0;
    std::string directionLabel;
    std::string gestureLabel;
    double rankScore = 0.0;
    double appearanceSimilarity = 0.0;
    double sceneSimilarity = 0.0;
    bool appearanceVerified = false;
    bool faceVerified = false;
    // Head-only geometry is not a measurement of the whole body pose.
    bool headOnlyComparison = false;
    // Ambiguous head geometry is ranked by directly observed orientation
    // before mirror-only alternatives, without changing the displayed score.
    double unmirroredSimilarity = -1.0;
};

class MotionMatcher {
public:
    explicit MotionMatcher(MotionMatcherParams params = {});

    const MotionMatcherParams& params() const noexcept { return params_; }
    void setParams(MotionMatcherParams params);

    // Compares two windows using normalized pose trajectories and constrained
    // DTW plus temporal/anatomical calibration. Returns a value in [0, 1];
    // the public score deliberately stays below 100% to avoid claiming proof
    // from a duplicated frame or a self-comparison.
    MotionMatch compare(const MotionWindow& left, const MotionWindow& right,
                        std::size_t leftIndex = 0, std::size_t rightIndex = 0) const;

    // Candidate search followed by shared per-shot selection. The default
    // returns each shot once; maxResultsPerShot explicitly controls reuse.
    // A requested stop throws std::runtime_error; no partial result is returned.
    std::vector<MotionMatch> findAllPairs(const std::vector<MotionWindow>& windows,
                                        std::stop_token stop = {}) const;

private:
    MotionMatcherParams params_;
};

} // namespace pfcore
