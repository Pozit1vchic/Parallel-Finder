#include <gtest/gtest.h>

#include <pfcore/SceneDetector.hpp>

#include <stdexcept>

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

} // namespace
