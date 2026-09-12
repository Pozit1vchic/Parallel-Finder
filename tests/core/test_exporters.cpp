#include <gtest/gtest.h>

#include <pfexporters/ExportOptions.hpp>

#include <filesystem>
#include <fstream>

namespace {

pfcore::MotionMatch sample()
{
    pfcore::MotionMatch result;
    result.leftSourceId = "a clip.mp4";
    result.rightSourceId = "b.csv";
    result.leftStartSeconds = 1.0;
    result.leftEndSeconds = 2.0;
    result.rightStartSeconds = 4.0;
    result.rightEndSeconds = 5.0;
    result.durationSeconds = 1.0;
    result.similarity = 0.91;
    return result;
}

TEST(Exporters, FormatsStructuredResults)
{
    const std::vector<pfcore::MotionMatch> matches {sample()};
    pfexporters::ExportOptions options;
    options.format = pfexporters::ExportFormat::Json;
    const std::string json = pfexporters::formatResults(matches, options);
    EXPECT_NE(json.find("a clip.mp4"), std::string::npos);
    EXPECT_NE(json.find("0.91"), std::string::npos);

    options.format = pfexporters::ExportFormat::Csv;
    EXPECT_NE(pfexporters::formatResults(matches, options).find("left_source"), std::string::npos);
    options.format = pfexporters::ExportFormat::Edl;
    EXPECT_NE(pfexporters::formatResults(matches, options).find("TITLE:"), std::string::npos);
}

TEST(Exporters, WritesAepPair)
{
    const auto directory = std::filesystem::temp_directory_path() / "parallel-finder-export-test";
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);

    pfexporters::ExportOptions options;
    options.format = pfexporters::ExportFormat::Aep;
    options.outputFolder = directory.string();
    options.filePrefix = "parallel";
    std::string error;
    ASSERT_TRUE(pfexporters::writeResults({sample()}, options, error)) << error;
    EXPECT_TRUE(std::filesystem::exists(directory / "parallel.jsx"));
    EXPECT_TRUE(std::filesystem::exists(directory / "parallel_data.json"));
    std::filesystem::remove_all(directory, ignored);
}

} // namespace
