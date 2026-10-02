#include "pfservices/PreviewWriter.hpp"
#include "pfservices/PreviewImage.hpp"
#include <QFile>
#include <QUrl>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace pfservices {
struct PreviewWriter::Impl {
    struct Task {
        QImage image;
        QString path;
        std::string key;
        std::promise<QString> done;
    };
    PfCache* cache;
    std::size_t capacity;
    std::mutex mutex;
    std::condition_variable available, space;
    std::deque<Task> tasks;
    bool closed = false;
    std::jthread worker;
    Impl(PfCache* value, std::size_t slotCount) : cache(value), capacity(slotCount)
    {
        if (slotCount == 0 || slotCount > 4) throw std::invalid_argument("PreviewWriter: invalid capacity");
        worker = std::jthread([this] {
            for (;;) {
                Task task;
                {
                    std::unique_lock lock(mutex);
                    available.wait(lock, [&] { return closed || !tasks.empty(); });
                    if (tasks.empty()) break;
                    task = std::move(tasks.front()); tasks.pop_front();
                }
                space.notify_one();
                QString url;
                try {
                    if (saveLosslessPreview(task.image, task.path)) {
                        url = QUrl::fromLocalFile(task.path).toString(QUrl::FullyEncoded);
                        if (cache) {
                            QFile file(task.path);
                            if (file.open(QIODevice::ReadOnly)) {
                                const auto bytes = file.readAll();
                                std::string error;
                                cache->put(task.key, std::vector<std::uint8_t>(bytes.begin(), bytes.end()), error);
                            }
                        }
                    }
                } catch (...) { /* Preserve serial preview failure/fallback behavior. */ }
                task.done.set_value(std::move(url));
            }
        });
    }
    void finish() noexcept
    {
        { std::lock_guard lock(mutex); closed = true; }
        available.notify_all(); space.notify_all();
        if (worker.joinable()) worker.join();
    }
};
PreviewWriter::PreviewWriter(PfCache* cache, std::size_t capacity) : impl_(std::make_unique<Impl>(cache, capacity)) {}
PreviewWriter::~PreviewWriter() { finish(); }
void PreviewWriter::finish() noexcept { impl_->finish(); }
std::shared_future<QString> PreviewWriter::submit(QImage image, QString path, std::string key)
{
    Impl::Task task{std::move(image), std::move(path), std::move(key), {}};
    auto future = task.done.get_future().share();
    std::unique_lock lock(impl_->mutex);
    impl_->space.wait(lock, [&] { return impl_->closed || impl_->tasks.size() < impl_->capacity; });
    if (impl_->closed) throw std::logic_error("PreviewWriter: submit after finish");
    impl_->tasks.push_back(std::move(task));
    lock.unlock(); impl_->available.notify_one();
    return future;
}
} // namespace pfservices
