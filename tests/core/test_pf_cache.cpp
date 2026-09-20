#include <gtest/gtest.h>

#include <pfservices/PfCache.hpp>

#include <filesystem>

TEST(PfCache, WritesReadsAndEvictsVersionedEntries)
{
    const auto root = std::filesystem::temp_directory_path() / "parallel-finder-pfcache-test";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    pfservices::PfCache cache(root, 180);
    std::string error;
    ASSERT_TRUE(cache.put("first", std::vector<std::uint8_t>(64, 1), error)) << error;
    ASSERT_TRUE(cache.get("first").has_value());
    ASSERT_TRUE(cache.put("second", std::vector<std::uint8_t>(128, 2), error)) << error;
    EXPECT_FALSE(cache.get("first").has_value());
    ASSERT_TRUE(cache.get("second").has_value());
    EXPECT_EQ(cache.get("second")->front(), 2U);
    EXPECT_GT(cache.bytesUsed(), 0U);
    ASSERT_TRUE(cache.clear(error)) << error;
    EXPECT_EQ(cache.bytesUsed(), 0U);
    std::filesystem::remove_all(root, ignored);
}

TEST(PfCache, RejectsOversizedEntry)
{
    const auto root = std::filesystem::temp_directory_path() / "parallel-finder-pfcache-limit-test";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    pfservices::PfCache cache(root, 32);
    std::string error;
    EXPECT_FALSE(cache.put("large", std::vector<std::uint8_t>(33, 1), error));
    EXPECT_FALSE(error.empty());
    std::filesystem::remove_all(root, ignored);
}
