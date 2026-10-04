#include <gtest/gtest.h>

#include <pfservices/PfCache.hpp>

#include <filesystem>
#include <algorithm>
#include <fstream>
#include <QTemporaryDir>
#include <QCoreApplication>
#include <atomic>
#include <thread>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

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

TEST(PfCache, UsesOwnFolderAndMigratesOnlyAuthenticatedLegacyEntries)
{
    const auto root = std::filesystem::temp_directory_path() / "parallel-finder-cache-layout-test";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    pfservices::PfCache cache(root);
    std::string message;
    ASSERT_TRUE(cache.put("owned-entry", {1,2,3}, message));
    const auto directory = root / "ParallelFinder-cache";
    ASSERT_TRUE(std::filesystem::is_directory(directory));
    const auto entry = std::filesystem::directory_iterator(directory)->path();
    const auto legacy = root / entry.filename();
    std::filesystem::rename(entry, legacy);
    { std::ofstream user(root / "source.mp4"); user << "video"; }
    { std::ofstream unknown(root / "unknown.pfc"); unknown << "not ours"; }
    pfservices::PfCache reopened(root);
    EXPECT_FALSE(std::filesystem::exists(legacy));
    ASSERT_TRUE(reopened.get("owned-entry"));
    EXPECT_EQ(*reopened.get("owned-entry"), (std::vector<std::uint8_t>{1,2,3}));
    ASSERT_TRUE(reopened.clear(message));
    EXPECT_TRUE(std::filesystem::exists(root / "source.mp4"));
    EXPECT_TRUE(std::filesystem::exists(root / "unknown.pfc"));
    std::filesystem::remove_all(root, error);
}

TEST(PfCache, RejectsTruncatedAndTrailingPayloadsAndRecoversByReplacement)
{
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    const auto root = std::filesystem::path(temporary.path().toStdWString());
    pfservices::PfCache cache(root);
    std::string error;
    const std::vector<std::uint8_t> payload {1, 2, 3};
    ASSERT_TRUE(cache.put("entry", payload, error)) << error;
    const auto path = std::filesystem::directory_iterator(root / "ParallelFinder-cache")->path();
    {
        std::ofstream append(path, std::ios::binary | std::ios::app);
        append.put('\0');
    }
    EXPECT_FALSE(cache.get("entry"));
    ASSERT_TRUE(cache.put("entry", payload, error)) << error;
    ASSERT_EQ(cache.get("entry"), payload);
    std::filesystem::resize_file(path, std::filesystem::file_size(path) - 1);
    EXPECT_FALSE(cache.get("entry"));
    ASSERT_TRUE(cache.put("entry", {}, error)) << error;
    ASSERT_TRUE(cache.get("entry"));
    EXPECT_TRUE(cache.get("entry")->empty());
}

TEST(PfCache, RejectsLargeDeclaredPayloadInTinyFile)
{
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    const auto root = std::filesystem::path(temporary.path().toStdWString());
    pfservices::PfCache cache(root);
    std::string error;
    ASSERT_TRUE(cache.put("entry", {1}, error)) << error;
    const auto path = std::filesystem::directory_iterator(root / "ParallelFinder-cache")->path();
    {
        std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
        ASSERT_TRUE(file);
        file.seekp(8 + sizeof(std::uint32_t) * 2);
        const std::uint64_t declared = 512ULL * 1024ULL * 1024ULL;
        file.write(reinterpret_cast<const char*>(&declared), sizeof(declared));
        ASSERT_TRUE(file);
    }
    // A tiny corrupt record must be a miss before allocating its declared
    // payload. Healthy files keep the original PFCACHE1 representation.
    EXPECT_FALSE(cache.get("entry"));
}

TEST(PfCache, DoesNotTruncatePreexistingTemporaryNames)
{
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    const auto root = std::filesystem::path(temporary.path().toStdWString());
    pfservices::PfCache cache(root);
    std::string error;
    ASSERT_TRUE(cache.put("entry", {1}, error)) << error;
    const auto path = std::filesystem::directory_iterator(root / "ParallelFinder-cache")->path();
    std::vector<std::filesystem::path> sentinels;
    // Run this regression alone against the frozen library to exercise its
    // predictable process/counter temporary name. None belongs to this put.
    for (unsigned i = 0; i < 64; ++i) {
        auto sentinel = std::filesystem::path(path.string() + ".tmp."
            + std::to_string(QCoreApplication::applicationPid()) + "." + std::to_string(i));
        std::ofstream output(sentinel, std::ios::binary);
        output << "unrelated-data";
        ASSERT_TRUE(output);
        sentinels.push_back(std::move(sentinel));
    }
    ASSERT_TRUE(cache.put("entry", {2}, error)) << error;
    EXPECT_EQ(cache.get("entry"), (std::vector<std::uint8_t>{2}));
    for (const auto& sentinel : sentinels) {
        std::ifstream input(sentinel, std::ios::binary);
        EXPECT_EQ(std::string(std::istreambuf_iterator<char>(input), {}), "unrelated-data")
            << sentinel.string();
    }
}

TEST(PfCache, FailedReplacementPreservesDirectoryAndRemovesOwnedTemporary)
{
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    const auto root = std::filesystem::path(temporary.path().toStdWString());
    pfservices::PfCache cache(root);
    std::string error;
    ASSERT_TRUE(cache.put("entry", {1}, error)) << error;
    const auto directory = root / "ParallelFinder-cache";
    const auto path = std::filesystem::directory_iterator(directory)->path();
    ASSERT_TRUE(std::filesystem::remove(path));
    ASSERT_TRUE(std::filesystem::create_directory(path));
    EXPECT_FALSE(cache.put("entry", {2}, error));
    EXPECT_FALSE(error.empty());
    EXPECT_TRUE(std::filesystem::is_directory(path));
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(directory),
                            std::filesystem::directory_iterator()), 1);
}

TEST(PfCache, ConcurrentReplacementExposesOnlyCompleteRecords)
{
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    const auto root = std::filesystem::path(temporary.path().toStdWString());
    pfservices::PfCache cache(root);
    std::string error;
    constexpr std::size_t payloadSize = 4096;
    ASSERT_TRUE(cache.put("entry", std::vector<std::uint8_t>(payloadSize, 1), error)) << error;
    pfservices::PfCache reopened(root / ".");
    auto copied = cache;
    std::atomic_bool finished{false};
    std::atomic_size_t invalidReads{0}, successfulWrites{0}, reads{0};
    std::vector<std::jthread> readers;
    for (unsigned i = 0; i < 2; ++i) {
        readers.emplace_back([&] {
            while (!finished.load(std::memory_order_acquire)) {
                const auto value = copied.get("entry");
                ++reads;
                if (!value || value->size() != payloadSize
                    || !std::all_of(value->begin(), value->end(), [&](auto b) { return b == value->front(); }))
                    ++invalidReads;
            }
        });
    }
    std::vector<std::jthread> writers;
    for (unsigned i = 0; i < 2; ++i) {
        writers.emplace_back([&, i] {
            for (unsigned n = 0; n < 100; ++n) {
                std::string message;
                // A sharing/commit failure is permitted; it must preserve the
                // previous complete record. Independent puts may race.
                auto& writer = i == 0 ? cache : reopened;
                if (writer.put("entry", std::vector<std::uint8_t>(payloadSize,
                              static_cast<std::uint8_t>(2 + i * 100 + n)), message))
                    ++successfulWrites;
            }
        });
    }
    writers.clear();
    finished.store(true, std::memory_order_release);
    readers.clear();
    EXPECT_GT(reads.load(), 0U);
    EXPECT_GT(successfulWrites.load(), 0U);
    EXPECT_EQ(invalidReads.load(), 0U);
    ASSERT_TRUE(cache.get("entry"));
    EXPECT_EQ(cache.get("entry")->size(), payloadSize);
}

#ifdef _WIN32
TEST(PfCache, SharingFailurePreservesPreviousRecordAndCleansStaging)
{
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    const auto root = std::filesystem::path(temporary.path().toStdWString());
    pfservices::PfCache cache(root);
    std::string error;
    const std::vector<std::uint8_t> original{1, 2, 3};
    ASSERT_TRUE(cache.put("entry", original, error)) << error;
    const auto directory = root / "ParallelFinder-cache";
    const auto path = std::filesystem::directory_iterator(directory)->path();
    // An external reader denies replacement. The failed atomic install must
    // neither remove the previous entry nor leave the new staging directory.
    std::unique_ptr<void, decltype(&CloseHandle)> held(
        CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr), &CloseHandle);
    ASSERT_NE(held.get(), INVALID_HANDLE_VALUE);
    EXPECT_FALSE(cache.put("entry", {4, 5, 6}, error));
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(cache.get("entry"), original);
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(directory),
                            std::filesystem::directory_iterator()), 1);
    held.reset();
    ASSERT_TRUE(cache.put("entry", {4, 5, 6}, error)) << error;
    EXPECT_EQ(cache.get("entry"), (std::vector<std::uint8_t>{4, 5, 6}));
}
#endif
