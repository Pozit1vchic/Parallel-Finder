#pragma once

#include <QPointer>
#include <QThread>
#include <algorithm>
#include <memory>
#include <vector>

namespace pfui {
// Own QThread::create workers until completion, including after the GUI event
// loop stops delivering deleteLater(). Join before their captured state dies.
class WorkerThreads final {
public:
    WorkerThreads() = default;
    WorkerThreads(const WorkerThreads&) = delete;
    WorkerThreads& operator=(const WorkerThreads&) = delete;
    ~WorkerThreads() { finish(); }
    void start(QThread* thread) {
        std::unique_ptr<QThread> guard(thread);
        std::erase_if(threads_, [](const auto& value) { return value.isNull(); });
        threads_.emplace_back(thread);
        QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);
        guard.release();
        thread->start();
    }
    void finish() noexcept {
        for (const auto& thread : threads_)
            if (thread) thread->requestInterruption();
        for (const auto& thread : threads_) {
            if (!thread) continue;
            thread->wait();
            delete thread.data();
        }
        threads_.clear();
    }
private:
    std::vector<QPointer<QThread>> threads_;
};
} // namespace pfui
