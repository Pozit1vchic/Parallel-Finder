#pragma once

#include <algorithm>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace pfcore {

// One ordered transform worker overlaps inference with the caller's next
// stage. Capacity includes queued inputs, the active input and ready outputs.
// The caller drains receive() when trySubmit() reports backpressure. Every
// accepted input owns its pixels; no borrowed frame can outlive the caller.
template<class Input, class Output>
class OrderedPipeline {
public:
    struct Result { Input input; Output output; };
    OrderedPipeline(std::size_t capacity, std::function<Output(const Input&)> transform,
                    std::function<bool()> cancelled = {})
        : capacity_(capacity), transform_(std::move(transform)), cancelled_(std::move(cancelled))
    {
        if (!capacity || capacity > 8 || !transform_) throw std::invalid_argument("Invalid pipeline capacity/transform");
        worker_ = std::jthread([this](std::stop_token stop) { work(stop); });
    }
    ~OrderedPipeline() { finish(); }
    OrderedPipeline(const OrderedPipeline&) = delete;
    OrderedPipeline& operator=(const OrderedPipeline&) = delete;

    bool trySubmit(Input& input)
    {
        std::lock_guard lock(mutex_);
        if (error_) std::rethrow_exception(error_);
        if (cancelled_ && cancelled_()) return false;
        if (closed_ || stopped_) {
            // Cancellation can land between the first predicate read and
            // the worker publishing stopped_. The caller still owns input.
            if (cancelled_ && cancelled_()) return false;
            throw std::logic_error("Submitting to a closed pipeline");
        }
        if (outstanding_ == capacity_) return false;
        inputs_.push_back(std::move(input));
        ++outstanding_; peak_ = std::max(peak_, outstanding_);
        ready_.notify_one();
        return true;
    }
    bool receive(Result& result, bool wait = true)
    {
        std::unique_lock lock(mutex_);
        if (wait) available_.wait(lock, [&] { return error_ || stopped_ || !outputs_.empty(); });
        if (error_) std::rethrow_exception(error_);
        if (outputs_.empty()) return false;
        result = std::move(outputs_.front()); outputs_.pop_front(); --outstanding_;
        return true;
    }
    void close()
    {
        std::lock_guard lock(mutex_); closed_ = true; ready_.notify_all();
    }
    void finish() noexcept
    {
        if (!worker_.joinable()) return;
        worker_.request_stop(); ready_.notify_all(); worker_.join();
    }
    std::size_t peakOutstanding() const { std::lock_guard lock(mutex_); return peak_; }
private:
    void work(std::stop_token stop) noexcept
    {
        try {
            for (;;) {
                Input input;
                {
                    std::unique_lock lock(mutex_);
                    if (!ready_.wait(lock, stop, [&] { return closed_ || !inputs_.empty(); })) break;
                    if (stop.stop_requested() || (cancelled_ && cancelled_())) break;
                    if (inputs_.empty()) break;
                    input = std::move(inputs_.front()); inputs_.pop_front();
                }
                Output output = transform_(input);
                if (stop.stop_requested() || (cancelled_ && cancelled_())) break;
                {
                    std::lock_guard lock(mutex_); outputs_.push_back({std::move(input), std::move(output)});
                }
                available_.notify_one();
            }
        } catch (...) { std::lock_guard lock(mutex_); error_ = std::current_exception(); }
        { std::lock_guard lock(mutex_); stopped_ = true; }
        available_.notify_all();
    }
    const std::size_t capacity_;
    std::function<Output(const Input&)> transform_;
    std::function<bool()> cancelled_;
    mutable std::mutex mutex_;
    std::condition_variable_any ready_;
    std::condition_variable available_;
    std::deque<Input> inputs_;
    std::deque<Result> outputs_;
    std::size_t outstanding_ = 0, peak_ = 0;
    bool closed_ = false, stopped_ = false;
    std::exception_ptr error_;
    std::jthread worker_; // join before destroying borrowed transform state
};
} // namespace pfcore
