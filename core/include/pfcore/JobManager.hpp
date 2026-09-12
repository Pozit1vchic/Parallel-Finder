#pragma once
#include <cstddef>
#include <future>
#include <functional>
#include <memory>
#include <string>
namespace pfcore {
class JobManager {
public:
    using Job = std::function<void()>;
    explicit JobManager(std::size_t maxQueued = 16,
                        std::size_t workerCount = 1);
    ~JobManager();
    JobManager(const JobManager&) = delete;
    JobManager& operator=(const JobManager&) = delete;
    std::future<void> submit(std::string sourcePath, Job job);
    void cancelPending();
    std::size_t pending() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace pfcore
