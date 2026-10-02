#include "pfcore/SceneDetector.hpp"
#include "pfcore/VideoDecoder.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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

double medianDeviation(std::span<const double> values, double median)
{
    std::vector<double> deviations;
    deviations.reserve(values.size());
    for (double value : values) deviations.push_back(std::abs(value - median));
    std::sort(deviations.begin(), deviations.end());
    return deviations.empty() ? 0.0 : deviations[deviations.size() / 2];
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

std::optional<double> SceneDetector::refineHardCut(VideoDecoder& decoder, double approximate,
                                                   std::stop_token stop) const
{
    if (stop.stop_requested() || !std::isfinite(approximate) || approximate <= 0.0) return {};
    const double fps = decoder.info().frameRate;
    const double frameDuration = fps > 0 ? 1.0 / fps : 1.0 / 24.0;
    if (approximate >= decoder.info().durationSeconds - frameDuration * 0.5) return {};
    const double start = std::max(0.0, approximate - 0.35 - frameDuration);
    // A sampled boundary is the first frame of the NEXT shot, so search
    // backwards; never move an end into a later, unrelated shot.
    decoder.setRgbaMaxDimensions(kCompactWidth, kCompactHeight);
    decoder.seek(start);
    CompactFrame previous;
    DecodedFrame frame;
    std::optional<double> best;
    while (!stop.stop_requested() && decoder.readNext(frame, false)) {
        if (frame.timestampSeconds > approximate + 1e-4) break;
        if (frame.timestampSeconds + 1e-6 < start) continue;
        if (!decoder.convertCurrentFrameToRgba(frame)) continue;
        auto current = compact({frame.timestampSeconds, frame.width, frame.height, frame.rgba});
        const double score = hsvDistance(previous, current);
        if (score >= threshold_
            && frame.timestampSeconds >= approximate - 0.35) {
            // Resolve the nearest preceding cut, not the strongest earlier
            // cut: several short shots may occur inside one sparse interval.
            best = frame.timestampSeconds;
        }
        previous = std::move(current);
    }
    return stop.stop_requested() ? std::nullopt : best;
}

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
    const bool debugScores = std::getenv("PF_DEBUG_SCENE_SCORES") != nullptr;

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
        // A cut is an outlier, not the normal motion level of the next shot.
        // A rolling mean containing the previous cut hid nearby real cuts.
        auto sortedRecent = recent;
        std::sort(sortedRecent.begin(), sortedRecent.end());
        const double localMean = sortedRecent.empty() ? deltas[i]
            : sortedRecent[sortedRecent.size() / 2];
        // A moving outgoing shot raises the past-only baseline and hides a
        // real cut into a quiet shot. Confirm that discontinuity against the
        // next three deltas as well (bounded lookahead on existing thumbnails,
        // no extra decoding). Ignore the next cut via the robust median, and
        // require a jump over the immediately preceding delta: merely slowing
        // camera motion must not manufacture a boundary.
        std::array<double, 3> following {};
        std::size_t followingCount = 0;
        for (std::size_t j = i + 1; j < deltas.size() && j <= i + 3; ++j)
            following[followingCount++] = deltas[j];
        if (followingCount == 3) std::sort(following.begin(), following.end());
        else if (followingCount == 2 && following[0] > following[1]) std::swap(following[0], following[1]);
        const double nextMedian = followingCount ? following[followingCount / 2] : 0.0;
        const bool forwardOutlier = followingCount >= 2
            && deltas[i] >= nextMedian * adaptiveMultiplier_
            && (i <= 1 || deltas[i] >= deltas[i - 1] * 1.5);
        // A cut between two steadily moving shots is not necessarily three
        // times the background motion level. Confirm a strong absolute jump
        // against BOTH local distributions, using their robust spread rather
        // than lowering the global HSV threshold or accepting camera slowdown.
        // The absolute floor and preceding jump also protect flat/noisy shots.
        const double spread = std::max({threshold_ * .25,
            medianDeviation(recent, localMean),
            medianDeviation(std::span(following.data(), followingCount), nextMedian)});
        const bool stableMotionCut = adaptiveMultiplier_ > 0.0 && recent.size() >= 3
            && followingCount >= 2 && deltas[i] >= threshold_ * 2.0
            && deltas[i] >= std::max(localMean, nextMedian) + adaptiveMultiplier_ * spread
            && deltas[i] >= deltas[i-1] * 1.5;
        const bool adaptivePass = adaptiveMultiplier_ == 0.0 || (recent.empty() && boundaries.empty())
            || deltas[i] >= localMean * adaptiveMultiplier_ || forwardOutlier || stableMotionCut;
        const bool validTimestamp = samples[i].timestampSeconds >= samples[i - 1].timestampSeconds;
        // Same-colour reverse angles/zoom cuts can be below the absolute HSV
        // threshold. Admit only an isolated discontinuity confirmed on BOTH
        // sides, with a substantial absolute floor (never detector noise).
        const bool lowContrastCut = !recent.empty() && forwardOutlier
            && deltas[i] >= localMean * adaptiveMultiplier_
            && deltas[i] >= threshold_ * .5;
        if (debugScores)
            std::fprintf(stderr, "PF_DEBUG_SCENE_SCORE t=%.6f delta=%.3f past=%.3f next=%.3f adaptive=%d low=%d stable=%d spacing=%zu\n",
                samples[i].timestampSeconds, deltas[i], localMean,
                followingCount ? following[followingCount / 2] : 0.0,
                adaptivePass ? 1 : 0, lowContrastCut ? 1 : 0, stableMotionCut ? 1 : 0, framesSinceBoundary);
        if (validTimestamp && !frames[i].pixels.empty()
            && (deltas[i] >= threshold_ || lowContrastCut)
            && adaptivePass && framesSinceBoundary >= minSceneFrames_) {
            boundaries.push_back({samples[i].timestampSeconds,
                                  boundaryScore(deltas[i], threshold_)});
            framesSinceBoundary = 0;
            recent.clear();
            ++framesSinceBoundary;
            continue;
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
        // Summing small deltas alone splits a continuous talking head or
        // moving camera into fake shots. A fade needs an actual sustained
        // whole-frame brightness change, not accumulated local motion.
        std::size_t eligible = 0, changing = 0;
        const auto& from = frames[softStart].pixels;
        const auto& to = frames[i].pixels;
        for (std::size_t p = 0; p < std::min(from.size(), to.size()); ++p) {
            if (from[p].value < 12 && to[p].value < 12) continue; // letterbox
            ++eligible;
            const double change = to[p].value - from[p].value;
            if (std::abs(change) >= threshold_ * .3
                && (change > 0 ? 1.0 : -1.0) == softDirection) ++changing;
        }
        const bool enoughChange = brightnessChange >= threshold_ * 0.7
            && eligible && changing * 5 >= eligible * 3;
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

SceneContextIndex::SceneContextIndex(std::span<const SceneSample> samples)
{
    timestamps_.reserve(samples.size()); prefix_.reserve(samples.size() + 1);
    prefix_.push_back({});
    for (const auto& sample : samples) {
        if (!std::isfinite(sample.timestampSeconds)
            || (!timestamps_.empty() && sample.timestampSeconds < timestamps_.back()))
            throw std::invalid_argument("SceneContextIndex: ordered finite timestamps required");
        timestamps_.push_back(sample.timestampSeconds);
        auto histogram = prefix_.back();
        if (sample.width <= 0 || sample.height <= 0
            || sample.rgba.size() < static_cast<std::size_t>(sample.width) * sample.height * 4) {
            prefix_.push_back(histogram); continue;
        }
        for (int y = sample.height / 8; y < sample.height * 7 / 8; ++y) {
            for (int x = 0; x < sample.width; ++x) {
                if (x > sample.width / 4 && x < sample.width * 3 / 4) continue;
                const auto i = (static_cast<std::size_t>(y) * sample.width + x) * 4;
                const auto hsv = toHsv(sample.rgba[i], sample.rgba[i+1], sample.rgba[i+2]);
                if (hsv.value < 16) continue;
                const int h = std::clamp(static_cast<int>(hsv.hue * 12), 0, 11);
                const int s = std::clamp(static_cast<int>(hsv.saturation * 3 / 256), 0, 2);
                const int v = std::clamp(static_cast<int>(hsv.value * 8 / 256), 0, 7);
                histogram[h * 3 + s] += 0.75F;
                histogram[36 + v] += 0.25F;
                histogram[44] += 1;
            }
        }
        prefix_.push_back(histogram);
    }
}

std::vector<float> SceneContextIndex::query(double start, double end) const
{
    if (!std::isfinite(start) || !std::isfinite(end) || end < start) return {};
    const auto first = static_cast<std::size_t>(std::lower_bound(timestamps_.begin(), timestamps_.end(), start) - timestamps_.begin());
    const auto last = static_cast<std::size_t>(std::upper_bound(timestamps_.begin(), timestamps_.end(), end) - timestamps_.begin());
    const double count = prefix_[last][44] - prefix_[first][44];
    if (count < 32) return {};
    std::vector<float> histogram(44);
    for (std::size_t i = 0; i < histogram.size(); ++i)
        histogram[i] = static_cast<float>((prefix_[last][i] - prefix_[first][i]) / count);
    return histogram;
}

std::vector<float> sceneContext(std::span<const SceneSample> samples, double start, double end)
{
    return SceneContextIndex(samples).query(start, end);
}

} // namespace pfcore
