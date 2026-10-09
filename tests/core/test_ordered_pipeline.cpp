#include <pfcore/OrderedPipeline.hpp>
#include <gtest/gtest.h>
#include <atomic>
#include <future>
#include <memory>
#include <vector>

TEST(OrderedPipeline, PreservesEveryOwnedInputInOrderUnderBackpressure)
{
    using Pipeline = pfcore::OrderedPipeline<std::unique_ptr<int>, int>;
    Pipeline pipeline(3, [](const auto& input) { return *input * 7; });
    std::vector<int> actual;
    const auto consume = [&](bool wait) {
        Pipeline::Result result;
        if (!pipeline.receive(result, wait)) return false;
        EXPECT_EQ(result.output, *result.input * 7);
        actual.push_back(*result.input); return true;
    };
    for (int i = 0; i < 1000; ++i) {
        auto input = std::make_unique<int>(i);
        while (!pipeline.trySubmit(input)) { ASSERT_TRUE(input); ASSERT_TRUE(consume(true)); }
        EXPECT_FALSE(input);
        while (consume(false)) {}
    }
    pipeline.close(); while (consume(true)) {}
    ASSERT_EQ(actual.size(), 1000U);
    for (int i = 0; i < 1000; ++i) EXPECT_EQ(actual[i], i);
    EXPECT_LE(pipeline.peakOutstanding(), 3U);
}

TEST(OrderedPipeline, FullReadyQueueCountsAgainstCapacityAndDoesNotConsumeRejectedInput)
{
    std::promise<void> release;
    const auto gate = release.get_future().share();
    pfcore::OrderedPipeline<int, int> pipeline(1, [gate](int input) { gate.wait(); return input; });
    int first = 3, second = 4;
    ASSERT_TRUE(pipeline.trySubmit(first));
    EXPECT_FALSE(pipeline.trySubmit(second)); EXPECT_EQ(second, 4);
    release.set_value();
    decltype(pipeline)::Result result;
    ASSERT_TRUE(pipeline.receive(result)); EXPECT_EQ(result.output, 3);
    EXPECT_TRUE(pipeline.trySubmit(second)); pipeline.close();
    ASSERT_TRUE(pipeline.receive(result)); EXPECT_EQ(result.output, 4);
    EXPECT_FALSE(pipeline.receive(result));
}

TEST(OrderedPipeline, PropagatesWorkerFailureAndJoinsWithoutPublishingPartialOutput)
{
    pfcore::OrderedPipeline<int, int> pipeline(2, [](int) -> int { throw std::runtime_error("inference failed"); });
    int input = 1; ASSERT_TRUE(pipeline.trySubmit(input));
    decltype(pipeline)::Result result;
    EXPECT_THROW(pipeline.receive(result), std::runtime_error);
    EXPECT_THROW(pipeline.trySubmit(input), std::runtime_error);
    pipeline.finish();
}

TEST(OrderedPipeline, CancellationDropsInFlightResultAndDestructionJoinsWorker)
{
    std::atomic_bool cancelled{false};
    std::promise<void> entered, release;
    const auto gate = release.get_future().share();
    pfcore::OrderedPipeline<int, int> pipeline(2, [&](int input) {
        entered.set_value(); gate.wait(); return input;
    }, [&] { return cancelled.load(); });
    int input = 1; ASSERT_TRUE(pipeline.trySubmit(input)); entered.get_future().wait();
    cancelled = true; release.set_value();
    decltype(pipeline)::Result result;
    EXPECT_FALSE(pipeline.receive(result)); pipeline.finish();
}

TEST(OrderedPipeline, CancelledSubmissionKeepsOwnedFrameBeforeAndAfterWorkerStops)
{
    std::atomic_bool cancelled{false};
    std::promise<void> entered, release;
    const auto gate = release.get_future().share();
    using Pipeline = pfcore::OrderedPipeline<std::unique_ptr<int>, int>;
    Pipeline pipeline(2, [&](const auto& input) {
        entered.set_value(); gate.wait(); return *input;
    }, [&] { return cancelled.load(); });
    auto first=std::make_unique<int>(1);ASSERT_TRUE(pipeline.trySubmit(first));entered.get_future().wait();
    cancelled=true;
    auto pending=std::make_unique<int>(2);
    EXPECT_FALSE(pipeline.trySubmit(pending));EXPECT_TRUE(pending);
    release.set_value();Pipeline::Result result;EXPECT_FALSE(pipeline.receive(result));
    auto afterStop=std::make_unique<int>(3);bool accepted=true;
    EXPECT_NO_THROW(accepted=pipeline.trySubmit(afterStop));
    EXPECT_FALSE(accepted);ASSERT_TRUE(afterStop);EXPECT_EQ(*afterStop,3);
    pipeline.finish();
}

TEST(OrderedPipeline, IdleAndClosedWorkersFinishWithoutDeadlock)
{
    pfcore::OrderedPipeline<int, int> pipeline(1, [](int input) { return input; });
    decltype(pipeline)::Result result;
    EXPECT_FALSE(pipeline.receive(result, false));
    pipeline.close(); EXPECT_FALSE(pipeline.receive(result));
    int input = 0; EXPECT_THROW(pipeline.trySubmit(input), std::logic_error);
    pipeline.finish(); pipeline.finish();
    { pfcore::OrderedPipeline<int, int> idle(1, [](int input) { return input; }); }
    EXPECT_THROW((pfcore::OrderedPipeline<int, int>(0, [](int input) { return input; })), std::invalid_argument);
}
