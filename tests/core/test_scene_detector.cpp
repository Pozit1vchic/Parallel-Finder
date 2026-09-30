#include <gtest/gtest.h>

#include <pfcore/SceneDetector.hpp>

#include <stdexcept>
#include <algorithm>

namespace {

TEST(SceneDetector, DefaultThresholdMatchesContentDeltaScale)
{
    pfcore::SceneDetector detector;
    EXPECT_DOUBLE_EQ(detector.threshold(), pfcore::SceneDetector::kDefaultThreshold);
    EXPECT_DOUBLE_EQ(detector.threshold(), 27.0);
}

TEST(SceneDetector, SetThresholdAcceptsPositiveValue)
{
    pfcore::SceneDetector detector;
    detector.setThreshold(0.55);
    EXPECT_DOUBLE_EQ(detector.threshold(), 0.55);
}

TEST(SceneDetector, RejectsNonPositiveThreshold)
{
    EXPECT_THROW(pfcore::SceneDetector(0.0), std::invalid_argument);
    EXPECT_THROW(pfcore::SceneDetector(-0.1), std::invalid_argument);

    pfcore::SceneDetector detector;
    EXPECT_THROW(detector.setThreshold(0.0), std::invalid_argument);
    EXPECT_THROW(detector.setThreshold(-1.0), std::invalid_argument);
    // Value unchanged after failed assignments.
    EXPECT_DOUBLE_EQ(detector.threshold(), pfcore::SceneDetector::kDefaultThreshold);
}

TEST(SceneDetector, EmptySamplesProduceNoBoundaries)
{
    pfcore::SceneDetector detector;
    EXPECT_TRUE(detector.detect().empty());
}

TEST(SceneContextIndex, OverlappingQueriesPreserveColourEvidenceAndInclusiveEdges)
{
    std::vector<std::uint8_t> red(16 * 16 * 4), blue(red.size()), dark(red.size());
    for (std::size_t i = 0; i < red.size(); i += 4) { red[i] = 255; blue[i+2] = 255; }
    const pfcore::SceneSample samples[] = {{0,16,16,red},{.25,16,16,blue},{.5,16,16,dark}};
    const pfcore::SceneContextIndex index(samples);
    const auto a = index.query(0, 0), b = index.query(.25, .25), both = index.query(0, .25);
    ASSERT_EQ(a.size(), 44U); ASSERT_EQ(b.size(), 44U); ASSERT_EQ(both.size(), 44U);
    for (std::size_t i = 0; i < both.size(); ++i) EXPECT_FLOAT_EQ(both[i], (a[i] + b[i]) / 2);
    EXPECT_EQ(index.query(0, .5), both); // black does not invent visual context
    EXPECT_TRUE(index.query(.5, .5).empty());
    EXPECT_TRUE(index.query(1, 2).empty());
    EXPECT_TRUE(index.query(2, 1).empty());
    EXPECT_EQ(pfcore::sceneContext(samples, .25, .25), b);
}

TEST(SceneDetector, DetectsStrongHistogramCut)
{
    std::vector<std::uint8_t> dark(4 * 4 * 4, 0);
    std::vector<std::uint8_t> bright(4 * 4 * 4, 255);
    const pfcore::SceneSample samples[] = {
        {0.0, 4, 4, dark},
        {1.0, 4, 4, bright},
    };
    // Use a one-frame minimum for this two-frame unit fixture. Production
    // defaults keep an eight-frame minimum to reject camera-motion spikes.
    pfcore::SceneDetector detector(27.0, 1, 0.0);
    const auto boundaries = detector.detect(samples);
    ASSERT_EQ(boundaries.size(), 1U);
    EXPECT_DOUBLE_EQ(boundaries.front().timestampSeconds, 1.0);
    EXPECT_GE(boundaries.front().score, 0.9);
}

TEST(SceneDetector, IgnoresSmallHistogramChangeBelowThreshold)
{
    std::vector<std::uint8_t> first(4 * 4 * 4, 0);
    std::vector<std::uint8_t> second = first;
    second[0] = 32;
    const pfcore::SceneSample samples[] = {
        {0.0, 4, 4, first},
        {1.0, 4, 4, second},
    };
    pfcore::SceneDetector detector(27.0, 1, 0.0);
    EXPECT_TRUE(detector.detect(samples).empty());
}

TEST(SceneDetector, FindsGradualFadeAsSoftBoundary)
{
    std::vector<std::vector<std::uint8_t>> frames;
    std::vector<pfcore::SceneSample> samples;
    for (int value = 0; value <= 64; value += 8) {
        frames.emplace_back(4 * 4 * 4, static_cast<std::uint8_t>(value));
        samples.push_back({static_cast<double>(frames.size() - 1), 4, 4, frames.back()});
    }
    pfcore::SceneDetector detector(27.0, 1, 0.0);
    const auto boundaries = detector.detect(samples);
    ASSERT_FALSE(boundaries.empty());
    EXPECT_GT(boundaries.front().timestampSeconds, 1.0);
    EXPECT_LT(boundaries.front().timestampSeconds, 7.0);
}

TEST(SceneDetector, PreviousCutDoesNotHideTheNextShot)
{
    std::vector<std::uint8_t> dark(4 * 4 * 4, 0);
    std::vector<std::uint8_t> bright(4 * 4 * 4, 255);
    const pfcore::SceneSample samples[] = {
        {0.00, 4, 4, dark}, {0.25, 4, 4, bright},
        {0.50, 4, 4, bright}, {0.75, 4, 4, dark}
    };
    const auto boundaries = pfcore::SceneDetector(27.0, 1, 3.0).detect(samples);
    ASSERT_EQ(boundaries.size(), 2U);
    EXPECT_DOUBLE_EQ(boundaries[1].timestampSeconds, 0.75);
}

TEST(SceneDetector, MotionBeforeCutCannotHideStableIncomingShot)
{
    std::vector<std::vector<std::uint8_t>> pixels;
    std::vector<pfcore::SceneSample> samples;
    // The past median is 50/3, so the 110 -> 240 jump (130/3) fails
    // a past-only multiplier of 3 despite a stable new shot afterwards.
    for (const int value : {10, 60, 10, 60, 110, 240, 240, 240, 240}) {
        pixels.emplace_back(4 * 4 * 4, static_cast<std::uint8_t>(value));
        samples.push_back({(pixels.size()-1) * .25, 4, 4, pixels.back()});
    }
    const auto boundaries = pfcore::SceneDetector(27, 1, 3).detect(samples);
    EXPECT_TRUE(std::any_of(boundaries.begin(), boundaries.end(), [](const auto& b) {
        return b.timestampSeconds == 1.25;
    }));
}

TEST(SceneDetector, ContinuousMotionDoesNotBecomeRepeatedHardCuts)
{
    std::vector<std::vector<std::uint8_t>> pixels;
    std::vector<pfcore::SceneSample> samples;
    // Equal high differences are movement, not isolated discontinuities.
    for (const int value : {0, 96, 0, 96, 0, 96, 0, 96, 0}) {
        pixels.emplace_back(4 * 4 * 4, static_cast<std::uint8_t>(value));
        samples.push_back({(pixels.size()-1) * .25, 4, 4, pixels.back()});
    }
    const auto boundaries = pfcore::SceneDetector(27, 1, 3).detect(samples);
    ASSERT_EQ(boundaries.size(), 1U); // only the initial change without baseline
    EXPECT_DOUBLE_EQ(boundaries[0].timestampSeconds, .25);
}

TEST(SceneDetector, IsolatedLowContrastCutNeedsEvidenceOnBothSides)
{
    std::vector<std::vector<std::uint8_t>> pixels;
    std::vector<pfcore::SceneSample> samples;
    for (const int value : {100,101,100,101,160,161,160,161}) {
        pixels.emplace_back(4 * 4 * 4, static_cast<std::uint8_t>(value));
        samples.push_back({(pixels.size()-1) * .25, 4, 4, pixels.back()});
    }
    const auto boundaries = pfcore::SceneDetector(27, 1, 3).detect(samples);
    ASSERT_EQ(boundaries.size(), 1U);
    EXPECT_DOUBLE_EQ(boundaries.front().timestampSeconds, 1);
    EXPECT_LT(boundaries.front().score, 1); // absolute delta below 27
}

TEST(SceneDetector, AccumulatedLocalMotionDoesNotInventFadeCuts)
{
    std::vector<std::vector<std::uint8_t>> pixels;
    std::vector<pfcore::SceneSample> samples;
    // A moving bright object against the same dark background changes HSV
    // positions but has constant global brightness: this is not a fade.
    for (int frame = 0; frame < 16; ++frame) {
        pixels.emplace_back(64 * 36 * 4, 0);
        for (int y = 8; y < 28; ++y) for (int x = 0; x < 16; ++x) {
            const auto offset = (y * 64 + (x + frame*2) % 64) * 4;
            pixels.back()[offset] = pixels.back()[offset+1] = pixels.back()[offset+2] = 240;
        }
        samples.push_back({frame * .25, 64, 36, pixels.back()});
    }
    EXPECT_TRUE(pfcore::SceneDetector(27, 1, 3).detect(samples).empty());
}

} // namespace
