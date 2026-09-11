#include <gtest/gtest.h>
#include <pfcore/JobManager.hpp>
#include <atomic>
#include <chrono>
#include <thread>

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
