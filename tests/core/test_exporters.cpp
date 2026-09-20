#include <gtest/gtest.h>

#include <pfexporters/ExportOptions.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>

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
    options.framesPerSecond = 23.976;
    options.format = pfexporters::ExportFormat::Edl;
    const std::string edl = pfexporters::formatResults(matches, options);
    EXPECT_NE(edl.find("TITLE:"), std::string::npos);
    EXPECT_NE(edl.find("NON-DROP FRAME"), std::string::npos);

    options.format = pfexporters::ExportFormat::FcpXml;
    const std::string xml = pfexporters::formatResults(matches, options);
    EXPECT_NE(xml.find("<asset-clip ref=\"a1\""), std::string::npos);
    EXPECT_NE(xml.find("file:///a%20clip.mp4"), std::string::npos);
    EXPECT_NE(xml.find("1001/24000s"), std::string::npos);
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
    options.framesPerSecond = 24.0;
    std::string error;
    ASSERT_TRUE(pfexporters::writeResults({sample()}, options, error)) << error;
    EXPECT_TRUE(std::filesystem::exists(directory / "parallel.jsx"));
    EXPECT_TRUE(std::filesystem::exists(directory / "parallel_data.json"));
    std::ifstream jsx(directory / "parallel.jsx");
    const std::string script((std::istreambuf_iterator<char>(jsx)), std::istreambuf_iterator<char>());
    EXPECT_NE(script.find("ImportOptions"), std::string::npos);
    EXPECT_NE(script.find("layers.add"), std::string::npos);
    std::filesystem::remove_all(directory, ignored);
}

TEST(Exporters, RejectsUnsafeOrUnprobedExports)
{
    const auto directory = std::filesystem::temp_directory_path() / "parallel-finder-export-validation";
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    pfexporters::ExportOptions options;
    options.outputFolder = directory.string();
    options.filePrefix = "../escape";
    std::string error;
    EXPECT_FALSE(pfexporters::writeResults({sample()}, options, error));
    EXPECT_NE(error.find("unsupported path characters"), std::string::npos);

    options.filePrefix = "results";
    options.format = pfexporters::ExportFormat::FcpXml;
    EXPECT_FALSE(pfexporters::writeResults({sample()}, options, error));
    EXPECT_NE(error.find("positive probed FPS"), std::string::npos);
    std::filesystem::remove_all(directory, ignored);
}

TEST(Exporters, NumberingModeControlsOrder)
{
    auto first = sample();
    first.leftStartSeconds = 10.0;
    first.rankScore = 0.9;
    auto second = sample();
    second.leftStartSeconds = 1.0;
    second.rankScore = 0.2;
    const std::vector<pfcore::MotionMatch> matches {first, second};
    pfexporters::ExportOptions options;
    options.format = pfexporters::ExportFormat::Json;
    options.numbering = pfexporters::NumberingMode::AsInVideo;
    const auto inVideo = pfexporters::formatResults(matches, options);
    EXPECT_LT(inVideo.find("\"start\": 1, \"end\""), inVideo.find("\"start\": 10, \"end\""));
    options.numbering = pfexporters::NumberingMode::RenumberSorted;
    const auto sorted = pfexporters::formatResults(matches, options);
    EXPECT_LT(sorted.find("\"start\": 10, \"end\""), sorted.find("\"start\": 1, \"end\""));
}

} // namespace
