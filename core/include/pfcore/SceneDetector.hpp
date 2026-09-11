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

// Scene change detector (stage 2b).
//
// Stage 0 stub: interface only. Candidate method to be finalized and justified
// in stage 2b: per-frame HSV histogram difference with an adaptive threshold
// (median of a sliding window), threshold tuned on video fixtures.
class SceneDetector {
public:
    // Placeholder default; the final value is justified and fixed in stage 2b.
    static constexpr double kDefaultThreshold = 0.30;

    explicit SceneDetector(double threshold = kDefaultThreshold);

    // Threshold must be strictly positive.
    // Throws std::invalid_argument otherwise.
    void setThreshold(double threshold);
    double threshold() const noexcept { return threshold_; }

    // Detects cuts from adjacent RGBA frame samples. Samples must be ordered
    // by timestamp; malformed/empty frames are skipped.
    std::vector<SceneBoundary> detect(std::span<const SceneSample> samples) const;

    // Convenience overload for callers that have no decoded samples yet.
    std::vector<SceneBoundary> detect() const;

private:
    double threshold_;
};

} // namespace pfcore
