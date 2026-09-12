#include "pfcore/SceneDetector.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace pfcore {
namespace {

constexpr int kCompactWidth = 64;
constexpr int kCompactHeight = 36;

struct Hsv {
    double hue = 0.0;
    double saturation = 0.0;
    double value = 0.0;
};

struct CompactFrame {
    std::vector<Hsv> pixels;
    double meanValue = 0.0;
};

Hsv toHsv(std::uint8_t red, std::uint8_t green, std::uint8_t blue)
{
    const double r = static_cast<double>(red) / 255.0;
    const double g = static_cast<double>(green) / 255.0;
    const double b = static_cast<double>(blue) / 255.0;
    const double maximum = std::max({r, g, b});
    const double minimum = std::min({r, g, b});
    const double delta = maximum - minimum;

    double hue = 0.0;
    if (delta > 1e-12) {
        if (maximum == r) hue = std::fmod((g - b) / delta, 6.0) / 6.0;
        else if (maximum == g) hue = ((b - r) / delta + 2.0) / 6.0;
        else hue = ((r - g) / delta + 4.0) / 6.0;
        if (hue < 0.0) hue += 1.0;
    }
    const double saturation = maximum <= 1e-12 ? 0.0 : delta / maximum;
    return {hue, saturation * 255.0, maximum * 255.0};
}

CompactFrame compact(const SceneSample& sample)
{
    CompactFrame result;
    if (sample.width <= 0 || sample.height <= 0 || sample.rgba.size() < 4) return result;
    result.pixels.reserve(kCompactWidth * kCompactHeight);
    for (int y = 0; y < kCompactHeight; ++y) {
        const int sourceY = std::min(sample.height - 1,
            (y * sample.height) / kCompactHeight);
        for (int x = 0; x < kCompactWidth; ++x) {
            const int sourceX = std::min(sample.width - 1,
                (x * sample.width) / kCompactWidth);
            const std::size_t offset = (static_cast<std::size_t>(sourceY) *
                                        static_cast<std::size_t>(sample.width) +
                                        static_cast<std::size_t>(sourceX)) * 4U;
            if (offset + 3U >= sample.rgba.size()) continue;
            result.pixels.push_back(toHsv(sample.rgba[offset], sample.rgba[offset + 1U],
                                          sample.rgba[offset + 2U]));
        }
    }
    if (!result.pixels.empty()) {
        result.meanValue = std::accumulate(result.pixels.begin(), result.pixels.end(), 0.0,
            [](double total, const Hsv& pixel) { return total + pixel.value; })
            / static_cast<double>(result.pixels.size());
    }
    return result;
}

double hsvDistance(const CompactFrame& left, const CompactFrame& right)
{
    if (left.pixels.empty() || right.pixels.empty()) return 0.0;
    const std::size_t count = std::min(left.pixels.size(), right.pixels.size());
    double total = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const double hueDelta = std::abs(left.pixels[i].hue - right.pixels[i].hue);
        const double wrappedHueDelta = std::min(hueDelta, 1.0 - hueDelta) * 255.0;
        const double saturationDelta = std::abs(left.pixels[i].saturation
                                                - right.pixels[i].saturation);
        const double valueDelta = std::abs(left.pixels[i].value - right.pixels[i].value);
        total += (wrappedHueDelta + saturationDelta + valueDelta) / 3.0;
    }
    return total / static_cast<double>(count);
}

double boundaryScore(double rawScore, double threshold)
{
    return std::max(0.0, rawScore / std::max(threshold, 1e-9));
}

} // namespace

SceneDetector::SceneDetector(double threshold, std::size_t minSceneFrames,
                             double adaptiveMultiplier)
    : threshold_(threshold)
    , minSceneFrames_(minSceneFrames)
    , adaptiveMultiplier_(adaptiveMultiplier)
{
    if (!(threshold_ > 0.0)) throw std::invalid_argument("SceneDetector: threshold must be > 0");
    if (minSceneFrames_ == 0) throw std::invalid_argument("SceneDetector: minSceneFrames must be >= 1");
    if (!(adaptiveMultiplier_ >= 0.0)) throw std::invalid_argument("SceneDetector: adaptiveMultiplier must be >= 0");
}

void SceneDetector::setThreshold(double threshold)
{
    if (!(threshold > 0.0)) throw std::invalid_argument("SceneDetector: threshold must be > 0");
    threshold_ = threshold;
}

std::vector<SceneBoundary> SceneDetector::detect() const { return {}; }

void SceneDetector::setMinSceneFrames(std::size_t value)
{
    if (value == 0) throw std::invalid_argument("SceneDetector: minSceneFrames must be >= 1");
    minSceneFrames_ = value;
}

void SceneDetector::setAdaptiveMultiplier(double value)
{
    if (!(value >= 0.0)) throw std::invalid_argument("SceneDetector: adaptiveMultiplier must be >= 0");
    adaptiveMultiplier_ = value;
}

std::vector<SceneBoundary> SceneDetector::detect(std::span<const SceneSample> samples) const
{
    std::vector<SceneBoundary> boundaries;
    if (samples.size() < 2) return boundaries;

    std::vector<CompactFrame> frames;
    frames.reserve(samples.size());
    for (const auto& sample : samples) frames.push_back(compact(sample));

    std::vector<double> deltas(samples.size(), 0.0);
    for (std::size_t i = 1; i < frames.size(); ++i)
        deltas[i] = hsvDistance(frames[i - 1], frames[i]);

    std::vector<double> recent;
    recent.reserve(5);
    std::size_t framesSinceBoundary = minSceneFrames_;
    for (std::size_t i = 1; i < samples.size(); ++i) {
        const double localMean = recent.empty()
            ? deltas[i]
            : std::accumulate(recent.begin(), recent.end(), 0.0)
                / static_cast<double>(recent.size());
        const bool adaptivePass = adaptiveMultiplier_ == 0.0 || recent.empty()
            || deltas[i] >= localMean * adaptiveMultiplier_;
        const bool validTimestamp = samples[i].timestampSeconds >= samples[i - 1].timestampSeconds;
        if (validTimestamp && !frames[i].pixels.empty() && deltas[i] >= threshold_
            && adaptivePass && framesSinceBoundary >= minSceneFrames_) {
            boundaries.push_back({samples[i].timestampSeconds,
                                  boundaryScore(deltas[i], threshold_)});
            framesSinceBoundary = 0;
            recent.clear();
        }
        ++framesSinceBoundary;
        recent.push_back(deltas[i]);
        if (recent.size() > 5) recent.erase(recent.begin());
    }

    // Second pass for fades and dissolves. A soft transition consists of a
    // short monotonic run whose individual deltas stay below the hard-cut
    // threshold but whose accumulated change is substantial.
    std::size_t softStart = 0;
    double softAccumulated = 0.0;
    double softDirection = 0.0;
    bool softBoundaryEmitted = false;
    for (std::size_t i = 1; i < samples.size(); ++i) {
        const double brightnessDelta = frames[i].meanValue - frames[i - 1].meanValue;
        const double direction = std::abs(brightnessDelta) < 1.0 ? 0.0
            : (brightnessDelta > 0.0 ? 1.0 : -1.0);
        const bool softFrame = deltas[i] < threshold_
            && (direction != 0.0 || deltas[i] >= threshold_ * 0.1);
        const bool directionChanged = softDirection != 0.0 && direction != 0.0
            && direction != softDirection;
        if (!softFrame || directionChanged) {
            softStart = i;
            softAccumulated = 0.0;
            softDirection = direction;
            softBoundaryEmitted = false;
            continue;
        }
        if (softBoundaryEmitted) continue;
        if (softAccumulated == 0.0) softStart = i - 1;
        softDirection = direction;
        softAccumulated += deltas[i];
        const std::size_t length = i - softStart + 1;
        const double brightnessChange = std::abs(frames[i].meanValue
                                                  - frames[softStart].meanValue);
        const bool enoughChange = brightnessChange >= threshold_ * 0.7
            || softAccumulated >= threshold_ * 1.6;
        if (length >= 3 && enoughChange) {
            const std::size_t boundaryIndex = softStart + length / 2;
            const bool tooCloseToExisting = std::any_of(boundaries.begin(), boundaries.end(),
                [&](const SceneBoundary& boundary) {
                    const auto it = std::lower_bound(samples.begin(), samples.end(),
                        boundary.timestampSeconds,
                        [](const SceneSample& sample, double timestamp) {
                            return sample.timestampSeconds < timestamp;
                        });
                    const auto boundaryPosition = static_cast<std::size_t>(it - samples.begin());
                    return boundaryPosition > boundaryIndex
                        ? boundaryPosition - boundaryIndex < minSceneFrames_
                        : boundaryIndex - boundaryPosition < minSceneFrames_;
                });
            if (!tooCloseToExisting && boundaryIndex < samples.size()) {
                boundaries.push_back({samples[boundaryIndex].timestampSeconds,
                                      boundaryScore(softAccumulated, threshold_)});
                softBoundaryEmitted = true;
            }
            softStart = i;
            softAccumulated = 0.0;
        }
    }

    std::sort(boundaries.begin(), boundaries.end(),
              [](const SceneBoundary& left, const SceneBoundary& right) {
                  return left.timestampSeconds < right.timestampSeconds;
              });
    boundaries.erase(std::unique(boundaries.begin(), boundaries.end(),
        [](const SceneBoundary& left, const SceneBoundary& right) {
            return std::abs(left.timestampSeconds - right.timestampSeconds) < 1e-9;
        }), boundaries.end());
    return boundaries;
}

} // namespace pfcore
