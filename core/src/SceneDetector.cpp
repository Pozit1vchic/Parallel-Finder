#include "pfcore/SceneDetector.hpp"

#include <stdexcept>
#include <array>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace pfcore {

SceneDetector::SceneDetector(double threshold, std::size_t minSceneFrames,
                             double adaptiveMultiplier)
    : threshold_(threshold)
    , minSceneFrames_(minSceneFrames)
    , adaptiveMultiplier_(adaptiveMultiplier)
{
    if (!(threshold_ > 0.0)) {
        throw std::invalid_argument("SceneDetector: threshold must be > 0");
    }
    if (minSceneFrames_ == 0) {
        throw std::invalid_argument("SceneDetector: minSceneFrames must be >= 1");
    }
    if (!(adaptiveMultiplier_ >= 0.0)) {
        throw std::invalid_argument("SceneDetector: adaptiveMultiplier must be >= 0");
    }
}

void SceneDetector::setThreshold(double threshold)
{
    if (!(threshold > 0.0)) {
        throw std::invalid_argument("SceneDetector: threshold must be > 0");
    }
    threshold_ = threshold;
}

std::vector<SceneBoundary> SceneDetector::detect() const
{
    return {};
}

void SceneDetector::setMinSceneFrames(std::size_t value)
{
    if (value == 0) {
        throw std::invalid_argument("SceneDetector: minSceneFrames must be >= 1");
    }
    minSceneFrames_ = value;
}

void SceneDetector::setAdaptiveMultiplier(double value)
{
    if (!(value >= 0.0)) {
        throw std::invalid_argument("SceneDetector: adaptiveMultiplier must be >= 0");
    }
    adaptiveMultiplier_ = value;
}

std::vector<SceneBoundary> SceneDetector::detect(std::span<const SceneSample> samples) const
{
    std::vector<SceneBoundary> boundaries;
    if (samples.size() < 2) return boundaries;

    // A compact RGB histogram is robust to small camera motion and avoids
    // treating a person briefly leaving the frame as a scene boundary.
    auto histogram = [](const SceneSample& sample) {
        std::array<double, 96> bins{};
        const std::size_t pixelCount = sample.rgba.size() / 4;
        if (sample.width <= 0 || sample.height <= 0 || pixelCount == 0) return bins;
        for (std::size_t p = 0; p < pixelCount; ++p) {
            const auto* px = sample.rgba.data() + p * 4;
            bins[px[0] >> 3] += 1.0;
            bins[32 + (px[1] >> 3)] += 1.0;
            bins[64 + (px[2] >> 3)] += 1.0;
        }
        const double scale = 1.0 / static_cast<double>(pixelCount);
        for (double& value : bins) value *= scale;
        return bins;
    };

    auto previous = histogram(samples.front());
    double previousTimestamp = samples.front().timestampSeconds;
    std::vector<double> recentScores;
    recentScores.reserve(5);
    std::size_t framesSinceBoundary = minSceneFrames_;
    for (std::size_t i = 1; i < samples.size(); ++i) {
        const auto current = histogram(samples[i]);
        double score = 0.0;
        for (std::size_t b = 0; b < current.size(); ++b) score += std::abs(current[b] - previous[b]);
        score *= 0.5; // L1 distance of normalized histograms, range [0, 3].
        const double gap = samples[i].timestampSeconds - previousTimestamp;
        const double localMean = recentScores.empty()
            ? score
            : std::accumulate(recentScores.begin(), recentScores.end(), 0.0)
                / static_cast<double>(recentScores.size());
        const bool adaptivePass = adaptiveMultiplier_ == 0.0
            || recentScores.empty()
            || score >= localMean * adaptiveMultiplier_;
        if (gap >= 0.0 && score >= threshold_ && adaptivePass
            && framesSinceBoundary >= minSceneFrames_ && !samples[i].rgba.empty()) {
            boundaries.push_back({samples[i].timestampSeconds, score});
            framesSinceBoundary = 0;
            recentScores.clear();
        }
        ++framesSinceBoundary;
        recentScores.push_back(score);
        if (recentScores.size() > 5) recentScores.erase(recentScores.begin());
        previous = current;
        previousTimestamp = samples[i].timestampSeconds;
    }
    return boundaries;
}

} // namespace pfcore
