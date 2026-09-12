#include "pfcore/JobManager.hpp"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

namespace pfcore {

struct JobManager::Impl {
    struct Item { std::string path; Job job; std::promise<void> promise; };
    mutable std::mutex mutex;
    std::condition_variable ready;
    std::deque<std::unique_ptr<Item>> queue;
    std::unordered_set<std::string> activePaths;
    std::size_t maxQueued;
    bool stopping = false;
    std::vector<std::thread> workers;

    explicit Impl(std::size_t max, std::size_t workerCount) : maxQueued(max)
    {
        workers.reserve(workerCount);
        for (std::size_t i = 0; i < workerCount; ++i) workers.emplace_back([this] { run(); });
    }

    void run()
    {
        for (;;) {
            std::unique_ptr<Item> item;
            {
                std::unique_lock lock(mutex);
                ready.wait(lock, [this] {
                    if (stopping) return true;
                    return std::any_of(queue.begin(), queue.end(), [&](const auto& candidate) {
                        return !activePaths.contains(candidate->path);
                    });
                });
                if (stopping && queue.empty()) return;
                const auto available = std::find_if(queue.begin(), queue.end(), [&](const auto& candidate) {
                    return !activePaths.contains(candidate->path);
                });
                if (available == queue.end()) continue;
                item = std::move(*available);
                queue.erase(available);
                activePaths.insert(item->path);
            }
            try {
                item->job();
                item->promise.set_value();
            } catch (...) {
                item->promise.set_exception(std::current_exception());
            }
            {
                std::lock_guard lock(mutex);
                activePaths.erase(item->path);
            }
            ready.notify_all();
        }
    }
};

JobManager::JobManager(std::size_t maxQueued, std::size_t workerCount)
    : impl_(std::make_unique<Impl>(maxQueued, workerCount))
{
    if (maxQueued == 0 || workerCount == 0)
        throw std::invalid_argument("JobManager: queue and worker counts must be > 0");
}

JobManager::~JobManager()
{
    {
        std::lock_guard lock(impl_->mutex);
        impl_->stopping = true;
    }
    impl_->ready.notify_all();
    for (auto& worker : impl_->workers) worker.join();
}

std::future<void> JobManager::submit(std::string sourcePath, Job job)
{
    if (!job) throw std::invalid_argument("JobManager: empty job");
    auto item = std::make_unique<Impl::Item>();
    item->path = std::move(sourcePath);
    item->job = std::move(job);
    auto future = item->promise.get_future();
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->stopping) throw std::runtime_error("JobManager is stopped");
        if (impl_->queue.size() >= impl_->maxQueued)
            throw std::overflow_error("JobManager queue is full");
        impl_->queue.push_back(std::move(item));
    }
    impl_->ready.notify_all();
    return future;
}

void JobManager::cancelPending()
{
    std::deque<std::unique_ptr<Impl::Item>> dropped;
    {
        std::lock_guard lock(impl_->mutex);
        dropped.swap(impl_->queue);
    }
    for (auto& item : dropped)
        item->promise.set_exception(std::make_exception_ptr(std::runtime_error("job cancelled")));
    impl_->ready.notify_all();
}

std::size_t JobManager::pending() const
{
    std::lock_guard lock(impl_->mutex);
    return impl_->queue.size();
}

} // namespace pfcore
