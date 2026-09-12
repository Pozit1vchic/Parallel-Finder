#include <gtest/gtest.h>

#include <pfservices/SettingsStore.hpp>

#include <QDir>

#include <filesystem>

namespace {

TEST(SettingsStore, SavesAndLoadsAtomically)
{
    const auto path = std::filesystem::temp_directory_path() / "parallel-finder-settings-test" / "settings.json";
    std::error_code ignored;
    std::filesystem::remove_all(path.parent_path(), ignored);

    pfservices::SettingsStore store(path.string());
    pfservices::Settings expected;
    expected.provider = "dml";
    expected.cacheLimitBytes = 123456;
    expected.sceneThreshold = 42.0;
    expected.sceneMinFrames = 6;
    expected.similarityThreshold = 0.91;
    expected.candidateThreshold = 0.61;
    expected.minRepeatGapSec = 7.0;
    expected.sameFileGapSec = 2.5;
    expected.crossFileGapSec = 1.25;
    expected.duplicateWindowSec = 1.75;
    expected.noiseFactor = 0.8;
    expected.maxUniqueResults = 321;
    expected.timeWeight = 0.35;
    expected.sakoeChibaRatio = 0.15;
    std::string error;
    ASSERT_TRUE(store.save(expected, error)) << error;

    const auto loaded = store.load(error);
    EXPECT_TRUE(error.empty()) << error;
    EXPECT_EQ(loaded.provider, "dml");
    EXPECT_EQ(loaded.cacheLimitBytes, 123456U);
    EXPECT_DOUBLE_EQ(loaded.sceneThreshold, 42.0);
    EXPECT_EQ(loaded.sceneMinFrames, 6U);
    EXPECT_DOUBLE_EQ(loaded.similarityThreshold, 0.91);
    EXPECT_DOUBLE_EQ(loaded.candidateThreshold, 0.61);
    EXPECT_DOUBLE_EQ(loaded.minRepeatGapSec, 7.0);
    EXPECT_DOUBLE_EQ(loaded.sameFileGapSec, 2.5);
    EXPECT_DOUBLE_EQ(loaded.crossFileGapSec, 1.25);
    EXPECT_DOUBLE_EQ(loaded.duplicateWindowSec, 1.75);
    EXPECT_DOUBLE_EQ(loaded.noiseFactor, 0.8);
    EXPECT_EQ(loaded.maxUniqueResults, 321U);
    EXPECT_DOUBLE_EQ(loaded.timeWeight, 0.35);
    EXPECT_DOUBLE_EQ(loaded.sakoeChibaRatio, 0.15);

    std::filesystem::remove_all(path.parent_path(), ignored);
}

} // namespace
