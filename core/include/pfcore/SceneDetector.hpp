#pragma once

#include <cstdint>
#include <span>
#include <vector>
#include <optional>
#include <stop_token>
#include <array>
#include <functional>
#include <string>

namespace pfcore {
class VideoDecoder;

// One detected scene/shot boundary inside a single source file.
// Scene detection is a whole-shot change (cut / fade / strong visual change),
// NOT "character left the frame" — see spec section 3.
struct SceneBoundary {
    double timestampSeconds; // boundary position on the file's local timeline
    double score;            // raw detector confidence (0..1+)
};

struct SceneSample {
    double timestampSeconds = 0.0;
    int width = 0;
    int height = 0;
    std::span<const std::uint8_t> rgba;
};

struct SceneViewRequest {double target=0, start=0, end=0;};
struct SceneViewObservation {double timestampSeconds=0; std::vector<float> pixels;};
// Exact decoded frames at or after each target. Nearby requests share forward
// decoding, while distant requests seek. Results retain caller order and bounds.
std::vector<SceneViewObservation> sampleSceneViews(VideoDecoder& decoder,
    std::span<const SceneViewRequest> requests, const std::function<bool()>& cancelled={},
    const std::function<void(std::size_t,std::size_t)>& progress={});

// Bounded parallel CPU decoding. Each worker owns its decoder; callbacks run
// only on the caller, and output order matches the supplied requests.
std::vector<SceneViewObservation> sampleSceneViewsParallel(const std::string& source,
    std::span<const SceneViewRequest> requests, const std::function<bool()>& cancelled={},
    const std::function<void(std::size_t,std::size_t)>& progress={}, std::size_t threadBudget=8,
    bool preferNvidia=false);

// Compact spatial colour layout. Rejects malformed, flat or nearly black
// images, which cannot independently establish a recurring camera view.
std::vector<float> sceneViewDescriptor(const SceneSample& sample);
// Normalize only observed black/pillarbox borders for copied-footage checks.
// The normal camera descriptor keeps its historical framing policy intact.
std::vector<float> sceneContentDescriptor(const SceneSample& sample);

// Background colour context, excluding the central actor region and black
// letterboxing. A heuristic for nearby shots of one setting, not identity.
std::vector<float> sceneContext(std::span<const SceneSample> samples,
                                double startSeconds, double endSeconds);

// Build colour histograms once, then query overlapping motion chunks in
// O(log samples + histogram bins), not by re-converting the same pixels.
class SceneContextIndex {
public:
    explicit SceneContextIndex(std::span<const SceneSample> samples);
    std::vector<float> query(double startSeconds, double endSeconds) const;
private:
    std::vector<double> timestamps_;
    std::vector<std::array<double, 45>> prefix_;
};

// Scene change detector (stage 2b). Frames are compared in compact HSV space
// with an adaptive short baseline. Hard cuts and slow fade/dissolve transitions
// are treated separately; a person briefly leaving the frame is not a scene.
class SceneDetector {
public:
    // HSV content delta is measured on a 0..255 scale, matching the documented
    // ContentDetector-style threshold. The returned boundary score is normalized
    // to 0..1+ for UI/export consumers.
    static constexpr double kDefaultThreshold = 27.0;
    static constexpr std::size_t kDefaultMinSceneFrames = 8;
    static constexpr double kDefaultAdaptiveMultiplier = 3.0;

    explicit SceneDetector(double threshold = kDefaultThreshold,
                           std::size_t minSceneFrames = kDefaultMinSceneFrames,
                           double adaptiveMultiplier = kDefaultAdaptiveMultiplier);

    // Threshold must be strictly positive.
    // Throws std::invalid_argument otherwise.
    void setThreshold(double threshold);
    double threshold() const noexcept { return threshold_; }
    void setMinSceneFrames(std::size_t value);
    std::size_t minSceneFrames() const noexcept { return minSceneFrames_; }
    void setAdaptiveMultiplier(double value);
    double adaptiveMultiplier() const noexcept { return adaptiveMultiplier_; }

    // Detects cuts from adjacent RGBA frame samples. Samples must be ordered
    // by timestamp; malformed/empty frames are skipped.
    std::vector<SceneBoundary> detect(std::span<const SceneSample> samples) const;
    std::vector<SceneBoundary> detectWithProgress(std::span<const SceneSample> samples,
        const std::function<bool()>& cancelled,
        const std::function<void(std::size_t,std::size_t)>& progress={}) const;

    // Convenience overload for callers that have no decoded samples yet.
    std::vector<SceneBoundary> detect() const;

    // The analysis uses sparse thumbnails; its hard-cut timestamp can be
    // up to one sampling interval late. Export resolves only the requested
    // nearby edge at native frame cadence. No evidence => no invented edge.
    std::optional<double> refineHardCut(VideoDecoder& decoder, double approximateSeconds,
                                        std::stop_token stop = {}) const;

private:
    double threshold_;
    std::size_t minSceneFrames_;
    double adaptiveMultiplier_;
};

} // namespace pfcore
