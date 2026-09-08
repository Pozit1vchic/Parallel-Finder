#include <gtest/gtest.h>

#include <pfcore/SceneDetector.hpp>

#include <stdexcept>

namespace {

TEST(SceneDetector, DefaultThresholdIsPlaceholder30)
{
    pfcore::SceneDetector detector;
    EXPECT_DOUBLE_EQ(detector.threshold(), pfcore::SceneDetector::kDefaultThreshold);
    EXPECT_DOUBLE_EQ(detector.threshold(), 0.30);
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

// Stage 0 stub: no frames analyzed yet, no boundaries produced.
TEST(SceneDetector, StubDetectReturnsEmpty)
{
    pfcore::SceneDetector detector;
    EXPECT_TRUE(detector.detect().empty());
}

} // namespace
