#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace pfcore {

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

    // Convenience overload for callers that have no decoded samples yet.
    std::vector<SceneBoundary> detect() const;

private:
    double threshold_;
    std::size_t minSceneFrames_;
    double adaptiveMultiplier_;
};

} // namespace pfcore
