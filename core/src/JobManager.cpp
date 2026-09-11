#include "pfcore/JobManager.hpp"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>
namespace pfcore {
struct JobManager::Impl {
    struct Item { std::string path; Job job; std::promise<void> promise; };
    mutable std::mutex mutex; std::condition_variable ready; std::deque<std::unique_ptr<Item>> queue;
    std::size_t maxQueued; bool stopping = false; std::thread worker;
    explicit Impl(std::size_t max) : maxQueued(max), worker([this] { run(); }) {}
    void run() { for (;;) { std::unique_ptr<Item> item; { std::unique_lock lock(mutex); ready.wait(lock, [this] { return stopping || !queue.empty(); }); if (stopping && queue.empty()) return; item = std::move(queue.front()); queue.pop_front(); } try { item->job(); item->promise.set_value(); } catch (...) { item->promise.set_exception(std::current_exception()); } } }
};
JobManager::JobManager(std::size_t maxQueued) : impl_(std::make_unique<Impl>(maxQueued)) { if (!maxQueued) throw std::invalid_argument("JobManager: maxQueued must be > 0"); }
JobManager::~JobManager() { { std::lock_guard lock(impl_->mutex); impl_->stopping = true; } impl_->ready.notify_one(); impl_->worker.join(); }
std::future<void> JobManager::submit(std::string path, Job job) { if (!job) throw std::invalid_argument("JobManager: empty job"); auto item = std::make_unique<Impl::Item>(); item->path = std::move(path); item->job = std::move(job); auto future = item->promise.get_future(); { std::lock_guard lock(impl_->mutex); if (impl_->stopping) throw std::runtime_error("JobManager is stopped"); if (impl_->queue.size() >= impl_->maxQueued) throw std::overflow_error("JobManager queue is full"); impl_->queue.push_back(std::move(item)); } impl_->ready.notify_one(); return future; }
void JobManager::cancelPending() { std::deque<std::unique_ptr<Impl::Item>> dropped; { std::lock_guard lock(impl_->mutex); dropped.swap(impl_->queue); } for (auto& item : dropped) item->promise.set_exception(std::make_exception_ptr(std::runtime_error("job cancelled"))); }
std::size_t JobManager::pending() const { std::lock_guard lock(impl_->mutex); return impl_->queue.size(); }
} // namespace pfcore
