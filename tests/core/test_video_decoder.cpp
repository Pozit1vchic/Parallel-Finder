#include <gtest/gtest.h>

#include <pfcore/VideoDecoder.hpp>

#include <filesystem>

namespace {

TEST(VideoDecoder, OpensFixtureReadsFramesAndSeeks)
{
    pfcore::VideoDecoder decoder;
    decoder.open((std::filesystem::path(PF_TEST_FIXTURE_DIR) / "tiny.mp4").string());
    ASSERT_TRUE(decoder.isOpen());
    EXPECT_EQ(decoder.info().width, 16);
    EXPECT_EQ(decoder.info().height, 12);
    EXPECT_GT(decoder.info().frameRate, 0.0);

    pfcore::DecodedFrame first;
    ASSERT_TRUE(decoder.readNext(first));
    EXPECT_EQ(first.width, 16);
    EXPECT_EQ(first.height, 12);
    EXPECT_EQ(first.rgba.size(), 16U * 12U * 4U);

    decoder.seek(0.5);
    pfcore::DecodedFrame afterSeek;
    ASSERT_TRUE(decoder.readNext(afterSeek));
    EXPECT_GE(afterSeek.timestampSeconds, 0.0);
}

TEST(VideoDecoder, RejectsMissingFile)
{
    pfcore::VideoDecoder decoder;
    EXPECT_THROW(decoder.open("this-file-does-not-exist.mp4"), std::runtime_error);
}

TEST(VideoDecoder, RepeatedPreviewSeeksMatchFreshDecoder)
{
    const auto path = (std::filesystem::path(PF_TEST_FIXTURE_DIR) / "tiny.mp4").string();
    pfcore::VideoDecoder reused;
    reused.open(path);
    for (const double target : {0.6, 0.1, 0.8, 0.3}) {
        auto at = [target](pfcore::VideoDecoder& decoder) {
            decoder.seek(target);
            pfcore::DecodedFrame frame;
            while (decoder.readNext(frame, false)) {
                if (frame.timestampSeconds + 1e-3 >= target) {
                    EXPECT_TRUE(decoder.convertCurrentFrameToRgba(frame));
                    return frame;
                }
            }
            return frame;
        };
        pfcore::VideoDecoder fresh;
        fresh.open(path);
        const auto expected = at(fresh);
        const auto actual = at(reused);
        ASSERT_FALSE(expected.rgba.empty());
        EXPECT_EQ(actual.timestampSeconds, expected.timestampSeconds);
        EXPECT_EQ(actual.rgba, expected.rgba);
    }
}

} // namespace
