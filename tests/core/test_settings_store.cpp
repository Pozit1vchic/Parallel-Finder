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
    std::string error;
    ASSERT_TRUE(store.save(expected, error)) << error;

    const auto loaded = store.load(error);
    EXPECT_TRUE(error.empty()) << error;
    EXPECT_EQ(loaded.provider, "dml");
    EXPECT_EQ(loaded.cacheLimitBytes, 123456U);
    EXPECT_DOUBLE_EQ(loaded.sceneThreshold, 42.0);
    EXPECT_EQ(loaded.sceneMinFrames, 6U);

    std::filesystem::remove_all(path.parent_path(), ignored);
}

} // namespace
