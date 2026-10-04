#include <gtest/gtest.h>
#include "../../ui/src/EstimatorCache.hpp"
#include <atomic>
#include <thread>

TEST(EstimatorCache, ReusesRecentConfigurationsAndReleasesLeastRecentOwner)
{
    pfui::EstimatorCache<int> cache;
    auto a = cache.getOrCreate("a", [] { return std::make_shared<int>(1); });
    auto b = cache.getOrCreate("b", [] { return std::make_shared<int>(2); });
    std::weak_ptr<int> oldB = b;
    b.reset();
    EXPECT_EQ(a, cache.getOrCreate("a", [] { return std::make_shared<int>(99); }));
    auto c = cache.getOrCreate("c", [] { return std::make_shared<int>(3); });
    EXPECT_TRUE(oldB.expired());
    EXPECT_EQ(cache.size(), 2U);
    auto d = cache.getOrCreate("d", [] { return std::make_shared<int>(4); });
    EXPECT_EQ(cache.size(), 2U);
    // Eviction releases only the cache reference; active work still owns a.
    EXPECT_EQ(*a, 1);
    EXPECT_NE(a, cache.getOrCreate("a", [] { return std::make_shared<int>(5); }));
    EXPECT_EQ(*c, 3);
    EXPECT_EQ(*d, 4);
}

TEST(EstimatorCache, FailedConstructionPreservesRetainedConfigurations)
{
    EXPECT_THROW(pfui::EstimatorCache<int>(0), std::invalid_argument);
    pfui::EstimatorCache<int> cache;
    auto a = cache.getOrCreate("a", [] { return std::make_shared<int>(1); });
    auto b = cache.getOrCreate("b", [] { return std::make_shared<int>(2); });
    EXPECT_THROW(cache.getOrCreate("c", []() -> std::shared_ptr<int> {
        throw std::runtime_error("construction failed");
    }), std::runtime_error);
    EXPECT_THROW(cache.getOrCreate("c", [] { return std::shared_ptr<int>{}; }), std::invalid_argument);
    EXPECT_EQ(cache.size(), 2U);
    EXPECT_EQ(a, cache.getOrCreate("a", [] { return std::make_shared<int>(99); }));
    EXPECT_EQ(b, cache.getOrCreate("b", [] { return std::make_shared<int>(99); }));
}

TEST(EstimatorCache, ConcurrentAcquisitionConstructsOneSharedInstance)
{
    pfui::EstimatorCache<int> cache;
    std::atomic_int constructions{0};
    std::vector<std::shared_ptr<int>> handles(8);
    std::vector<std::jthread> workers;
    for (std::size_t i = 0; i < handles.size(); ++i) {
        workers.emplace_back([&, i] {
            for (int repetition = 0; repetition < 16; ++repetition)
                handles[i] = cache.getOrCreate("shared", [&] {
                    ++constructions;
                    return std::make_shared<int>(42);
                });
        });
    }
    workers.clear();
    EXPECT_EQ(constructions.load(), 1);
    EXPECT_EQ(cache.size(), 1U);
    for (const auto& handle : handles) {
        EXPECT_EQ(handle, handles.front());
        EXPECT_EQ(*handle, 42);
    }
}
