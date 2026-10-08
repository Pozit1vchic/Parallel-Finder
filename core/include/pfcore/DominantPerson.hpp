#pragma once

#include <cstddef>
#include <optional>
#include <functional>
#include <vector>

#include "pfcore/MotionMatcher.hpp"

namespace pfcore {

struct BoundingBox {
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    double bottom = 0.0;

    double area() const noexcept;
    double iou(const BoundingBox& other) const noexcept;
};

struct PersonDetection {
    double timestampSeconds = 0.0;
    double frameDurationSeconds = 0.0;
    BoundingBox box;
    double confidence = 0.0;
    double keypointConfidence = 0.0;
    std::vector<Keypoint> keypoints;
    // Optional appearance embedding from the body-ReID model.  It is kept on
    // the observation so a track prototype can be built after association.
    std::vector<float> appearanceEmbedding;
    double appearanceConfidence = 0.0;
    std::vector<float> faceEmbedding;
};

struct PersonTrack {
    std::size_t id = 0;
    std::vector<PersonDetection> observations;

    double totalTimeSeconds() const noexcept;
    double averageArea() const noexcept;
    double averageKeypointConfidence() const noexcept;
    double firstTimestampSeconds() const noexcept;
};

class DominantPersonTracker {
public:
    explicit DominantPersonTracker(double iouThreshold = 0.30,
                                   double maxGapSeconds = 1.0,
                                   bool identityRetirement = true);

    void reset();
    void update(double timestampSeconds,
                double frameDurationSeconds,
                const std::vector<PersonDetection>& detections);
    // Reassociate saved detections after shot boundaries become available.
    // Geometry may link observations only within one shot; identity grouping
    // across shots is performed separately using appearance evidence.
    void retrackScenes(const std::vector<double>& boundaries);
    const std::vector<PersonTrack>& tracks() const noexcept { return tracks_; }
    std::optional<PersonTrack> dominant() const;

private:
    double iouThreshold_;
    double maxGapSeconds_;
    bool identityRetirement_;
    std::size_t nextId_ = 1;
    std::size_t sceneTrackStart_ = 0;
    std::vector<PersonTrack> tracks_;
    // A geometric track contradicted by an independently observed identity
    // cannot resume on the next unsampled frame. A reappearing lead starts
    // a new fragment and is grouped by actual appearance evidence.
    std::vector<bool> retiredTracks_;
};

// New code should use the neutral name: the tracker maintains every active
// person track.  Keep the historical type name as the implementation/API
// compatibility surface for existing integrations.
using PersonTracker = DominantPersonTracker;

struct IdentitySummary {
    std::vector<float> body;
    std::vector<float> face;
    double bodyEvidence = 0;
    double faceEvidence = 0;
    double duration = 0;
    double area = 0;
    // Ordered source observation times. Concurrent detections are distinct
    // people, even when similar face/clothing embeddings suggest otherwise.
    std::vector<double> observationTimes;
};

// Select the longest identity, without allowing a body-only intermediate
// track to join two groups whose observed faces disagree. A single face
// sample can veto a clothing link without establishing a positive face link.
enum class DominantSelectionStage {Link,Cluster,Recovery};
struct DominantSelectionControl {
    // May be called by graph workers; use an atomic cancellation flag.
    std::function<bool()> cancelled;
    // Called only on the invoking thread.
    std::function<void(DominantSelectionStage,std::size_t,std::size_t)> progress;
    // Zero selects bounded CPU parallelism. One enables exact serial replay.
    std::size_t maxThreads = 0;
};
std::vector<bool> selectDominantIdentities(const std::vector<IdentitySummary>& identities,
    const DominantSelectionControl& control={});

struct DominantVideoSelection {
    std::vector<bool> windows;
    std::vector<std::string> sources;
    std::size_t suppliedSources = 0;
};

// Input windows must already belong to the independently admitted source lead.
// For multiple files, select ONE corroborated identity group across sources.
// Use independent shot face prototypes, never sliding-window counts or local
// timestamps. Complete-link agreement prevents a clothing/face bridge between
// different people. A single source keeps its existing admission unchanged.
DominantVideoSelection selectDominantVideoWindows(const std::vector<MotionWindow>& windows);

struct TrackObservationRange {
    std::size_t begin = 0;
    std::size_t end = 0; // exclusive
};

struct DominantSourceSelection {
    std::vector<bool> tracks;
    std::vector<bool> recovered;
    std::vector<std::vector<TrackObservationRange>> observationRuns;
};

// When rc16Tracks is supplied, it must be an independent legacy association of
// the same raw detections and shot cuts. Intersect initial admission by actual
// timestamp/box, so changed track components cannot promote unverified extras.
// Retain the rc16 lead group and recover only directly corroborated fragments:
// majority agreement with original face anchors plus body evidence. Recovered
// ranges end at actual face samples; no extrapolation into unsampled tails.
DominantSourceSelection selectDominantSourceTracks(const std::vector<PersonTrack>& tracks,
    const std::vector<PersonTrack>* rc16Tracks = nullptr,
    const DominantSelectionControl& control={});

// Select the main identity within [startSeconds, endSeconds), using only
// evidence observed in that shot. A poor profile/outfit match elsewhere in
// the file must not discard this shot before pairwise identity verification.
// A nonempty preferredTracks mask restricts selection to the file's lead.
// If that identity is absent or has fewer than two observations, select
// nothing. Shot-local selection is used only when no mask is supplied.
// Observations are timestamp-ordered, as maintained by PersonTracker.
std::vector<bool> selectDominantSceneTracks(const std::vector<PersonTrack>& tracks,
                                          double startSeconds, double endSeconds,
                                          const std::vector<bool>& preferredTracks = {});

} // namespace pfcore
