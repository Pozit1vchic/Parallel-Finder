#pragma once

#include <cstddef>
#include <string>
#include <vector>

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
    // Matching still uses the chunk; export can cut the complete scene.
    double sceneStartSeconds = -1.0;
    double sceneEndSeconds = -1.0;
    std::vector<PoseFrame> frames;
    // L2-normalized body-ReID prototype for this temporal window. Empty means
    // the optional ReID model was unavailable and the result is pose-only.
    std::vector<float> appearanceEmbedding;
    double appearanceConfidence = 0.0;
};

struct MotionMatcherParams {
    double similarityThreshold = 0.85;
    double candidateThreshold = 0.55;
    double minRepeatGapSec = 6.0;
    double sameFileGapSec = 2.0;
    double crossFileGapSec = 0.0;
    double duplicateWindowSec = 1.5;
    double noiseFactor = 1.0;
    std::size_t maxUniqueResults = 100;
    double timeWeight = 0.25;
    bool normalizeSize = true;
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
    double temporalSimilarityThreshold = 0.82;
    std::size_t minTemporalFrames = 12;
    double minTemporalDurationSec = 0.75;
    // Same-source windows never match inside this hard floor.  It prevents a
    // static shot from being paired with a nearby overlapping crop.
    double sameSourceGapFloorSec = 5.0;
    double nmsOverlapThreshold = 0.35;
    // Track IDs are local to a source file.  When both windows come from one
    // file, requiring the same ID prevents a pose from person A being matched
    // to a visually similar pose from person B.  Cross-file IDs are not
    // comparable and are therefore intentionally ignored.
    bool requireSameTrackWithinSource = true;
    bool allowStaticFrames = false;
    bool requireAppearance = false;
    double minAppearanceSimilarity = 0.55;
    double appearanceWeight = 0.20;
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
    bool appearanceVerified = false;
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

    // All-pairs search. Windows are never consumed: one window may participate
    // in several independent results, as required by the product semantics.
    std::vector<MotionMatch> findAllPairs(const std::vector<MotionWindow>& windows) const;

private:
    MotionMatcherParams params_;
};

} // namespace pfcore
