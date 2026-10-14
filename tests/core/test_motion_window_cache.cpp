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
TEST(MotionWindowCache, DirectFaceModelAndObservationsRoundTripWithoutChangingLegacyVersion) {
    pfcore::MotionWindow window;window.sourceId="direct-face.mp4";
    window.frames={pfcore::PoseFrame{.5,{}}};
    const auto legacy=pfservices::serializeMotionWindows(std::array{window},1);
    std::uint32_t version=0;std::memcpy(&version,legacy.data(),sizeof(version));EXPECT_EQ(version,9U);
    window.directFaceModel="sface-model-sha:forward-v1";window.directFaces={{.5,{1,0}}};
    const auto bytes=pfservices::serializeMotionWindows(std::array{window},1);
    std::memcpy(&version,bytes.data(),sizeof(version));EXPECT_EQ(version,10U);
    std::vector<pfcore::MotionWindow> restored;int scenes=0;
    ASSERT_TRUE(pfservices::deserializeMotionWindows(bytes,restored,scenes));
    EXPECT_EQ(restored[0].directFaceModel,window.directFaceModel);
    ASSERT_EQ(restored[0].directFaces.size(),1U);EXPECT_EQ(restored[0].directFaces[0].embedding,(std::vector<float>{1,0}));
    for(std::size_t n=0;n<bytes.size();++n)
        EXPECT_FALSE(pfservices::deserializeMotionWindows(std::span(bytes).first(n),restored,scenes));
    ASSERT_TRUE(pfservices::deserializeMotionWindows(legacy,restored,scenes));
    EXPECT_TRUE(restored[0].directFaceModel.empty());EXPECT_TRUE(restored[0].directFaces.empty());
}
TEST(MotionWindowCache, ResultHeadRefinementNeverChangesTheEstablishedSourcePayload) {
    pfcore::MotionWindow window;window.sourceId="preserved-source.mp4";
    window.frames.push_back({});window.frames.back().timestampSeconds=.5;
    const auto original=pfservices::serializeMotionWindows(std::array{window},1);
    window.headOrientations.push_back({.5,{1,0,0,0,1,0,0,0,1},true});
    EXPECT_EQ(pfservices::serializeMotionWindows(std::array{window},1),original);
    std::vector<pfcore::MotionWindow> restored;int scenes=0;
    ASSERT_TRUE(pfservices::deserializeMotionWindows(original,restored,scenes));
    ASSERT_EQ(restored.size(),1U);EXPECT_TRUE(restored[0].headOrientations.empty());
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
