#pragma once

#include <cstddef>
#include <optional>
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
                                   double maxGapSeconds = 1.0);

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
    std::size_t nextId_ = 1;
    std::size_t sceneTrackStart_ = 0;
    std::vector<PersonTrack> tracks_;
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
// Face-free members need direct body evidence to a recognisable face anchor;
// clothing-only transitive chains do not establish the lead's identity.
std::vector<bool> selectDominantIdentities(const std::vector<IdentitySummary>& identities);

struct TrackObservationRange {
    std::size_t begin = 0;
    std::size_t end = 0; // exclusive, indexes into the original track
};

struct DominantSourceSelection {
    std::vector<bool> tracks;
    std::vector<bool> recovered;
    // Original lead tracks retain their full range. A recovered track admits
    // only separately face-supported runs, never an unverified trailing cut.
    std::vector<std::vector<TrackObservationRange>> observationRuns;
};

// Recover profile fragments with a strict majority of ORIGINAL face anchors
// and a body link to a face-confirmed anchor. No transitive recovery. Consumers
// must respect observationRuns and bound playback/export to recovered runs.
DominantSourceSelection selectDominantSourceTracks(const std::vector<PersonTrack>& tracks);

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
