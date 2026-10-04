#include <gtest/gtest.h>
#include <pfcore/JobManager.hpp>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <thread>
#include <memory>


TEST(JobManager, RejectsInvalidConstructionBeforeStartingWorkers)
{
    EXPECT_THROW((pfcore::JobManager(0, 1)), std::invalid_argument);
    EXPECT_THROW((pfcore::JobManager(1, 0)), std::invalid_argument);
}

TEST(JobManager, ExecutesJobsAndPropagatesExceptions)
{
    pfcore::JobManager manager(2); std::atomic<int> count = 0;
    auto first = manager.submit("a", [&] { ++count; });
    auto second = manager.submit("b", [] { throw std::runtime_error("boom"); });
    first.get(); EXPECT_THROW(second.get(), std::runtime_error); EXPECT_EQ(count, 1);
}

TEST(JobManager, AppliesQueueBackpressure)
{
    pfcore::JobManager manager(1); std::promise<void> gate; auto hold = gate.get_future().share(); std::atomic<bool> started = false;
    auto first = manager.submit("a", [hold, &started] { started = true; hold.wait(); });
    for (int i = 0; i < 100 && !started; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    auto second = manager.submit("b", [] {});
    EXPECT_THROW(manager.submit("c", [] {}), std::overflow_error);
    gate.set_value(); first.get(); second.get();
}

TEST(JobManager, SerializesJobsWithTheSameSourceButAllowsDifferentSources)
{
    pfcore::JobManager manager(8, 2);
    std::promise<void> gate;
    auto hold = gate.get_future().share();
    std::atomic<int> activeA = 0;
    std::atomic<int> maxActiveA = 0;
    auto first = manager.submit("same", [hold, &activeA, &maxActiveA] {
        const int now = ++activeA;
        maxActiveA.store(std::max(maxActiveA.load(), now));
        hold.wait();
        --activeA;
    });
    auto second = manager.submit("same", [&activeA, &maxActiveA] {
        const int now = ++activeA;
        maxActiveA.store(std::max(maxActiveA.load(), now));
        --activeA;
    });
    auto different = manager.submit("other", [] {});
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_EQ(maxActiveA.load(), 1);
    gate.set_value();
    first.get(); second.get(); different.get();
}

TEST(JobManager, ShutdownDrainsQueuedSameSourceJobsAfterActiveJobFinishes)
{
    auto manager = std::make_unique<pfcore::JobManager>(8, 3);
    std::promise<void> started, gate, destroyed;
    const auto hold = gate.get_future().share();
    auto ready = started.get_future();
    auto finished = destroyed.get_future();
    std::atomic<int> completed = 0;
    auto first = manager->submit("same", [&] {
        started.set_value();
        hold.wait();
        ++completed;
    });
    ready.wait();
    auto second = manager->submit("same", [&] {
        EXPECT_EQ(completed.fetch_add(1), 1);
    });
    auto third = manager->submit("same", [&] {
        EXPECT_EQ(completed.fetch_add(1), 2);
    });
    std::jthread shutdown([owner = std::move(manager), &destroyed]() mutable {
        owner.reset();
        destroyed.set_value();
    });
    EXPECT_EQ(finished.wait_for(std::chrono::milliseconds(20)), std::future_status::timeout);
    gate.set_value();
    first.get(); second.get(); third.get();
    EXPECT_EQ(finished.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_EQ(completed.load(), 3);
}
