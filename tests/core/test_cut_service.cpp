#include <gtest/gtest.h>

#include <pfservices/CutService.hpp>

#include <filesystem>

TEST(CutService, RejectsInvalidRangeBeforeStartingFfmpeg)
{
    pfservices::CutService service("definitely-not-a-real-ffmpeg");
    pfservices::CutRequest request;
    request.inputPath = "missing.mp4";
    request.outputPath = "out.mp4";
    request.startSeconds = 4.0;
    request.endSeconds = 2.0;

    const auto result = service.cut(request);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error, "invalid cut time range");
}

TEST(CutService, RejectsMissingInputWithoutCreatingOutput)
{
    const auto output = std::filesystem::temp_directory_path() / "parallel-finder-cut-test.mp4";
    std::error_code ignored;
    std::filesystem::remove(output, ignored);

    pfservices::CutService service("definitely-not-a-real-ffmpeg");
    pfservices::CutRequest request;
    request.inputPath = "missing.mp4";
    request.outputPath = output;
    request.startSeconds = 0.0;
    request.endSeconds = 1.0;

    const auto result = service.cut(request);
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(std::filesystem::exists(output));
}
