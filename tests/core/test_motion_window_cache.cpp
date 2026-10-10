#include <pfservices/MotionWindowCache.hpp>
#include <gtest/gtest.h>
#include <array>
#include <cstring>

TEST(MotionWindowCache, RejectsForgedCountBeforeReservingWindows) {
    const std::array<std::uint32_t, 3> header{9, 100000, 1};
    std::vector<std::uint8_t> bytes(sizeof(header));
    std::memcpy(bytes.data(), header.data(), sizeof(header));
    std::vector<pfcore::MotionWindow> windows;
    int scenes = 0;
    EXPECT_FALSE(pfservices::deserializeMotionWindows(bytes, windows, scenes));
    EXPECT_EQ(windows.capacity(), 0u);
}
TEST(MotionWindowCache, RoundTripsAndRejectsEveryTruncation) {
    pfcore::MotionWindow window;
    window.sourceId = "sample.mp4";
    window.sceneStartSeconds = 0;
    window.sceneEndSeconds = 1;
    window.frames.push_back({});
    window.frames.back().timestampSeconds = 0.5;
    window.frames.back().keypoints.resize(17);
    const auto bytes = pfservices::serializeMotionWindows(std::array{window}, 1);
    std::vector<pfcore::MotionWindow> restored;
    int scenes = 0;
    ASSERT_TRUE(pfservices::deserializeMotionWindows(bytes, restored, scenes));
    ASSERT_EQ(restored.size(), 1u);
    EXPECT_EQ(restored[0].sourceId, window.sourceId);
    EXPECT_EQ(restored[0].frames[0].keypoints.size(), 17u);
    EXPECT_DOUBLE_EQ(restored[0].frames[0].timestampSeconds, 0.5);
    EXPECT_EQ(scenes, 1);
    for (std::size_t n=0; n<bytes.size(); ++n) {
        std::vector<pfcore::MotionWindow> output;
        EXPECT_FALSE(pfservices::deserializeMotionWindows(std::span(bytes).first(n), output, scenes)) << n;
    }
}
