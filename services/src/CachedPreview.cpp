#include "pfservices/CachedPreview.hpp"
#include <algorithm>
#include <atomic>
#include <thread>
#include <unordered_map>

namespace pfservices {
QString materializeCachedPreview(PfCache* cache, const std::string& key, const QString& directory)
{
    if (!cache || directory.isEmpty()) return {};
    const auto bytes = cache->get(key);
    if (!bytes || bytes->empty()) return {};
    const QByteArray encoded(reinterpret_cast<const char*>(bytes->data()), static_cast<qsizetype>(bytes->size()));
    if (QImage::fromData(encoded, "PNG").isNull()) return {};
    const auto name = QCryptographicHash::hash(QByteArray::fromStdString(key), QCryptographicHash::Sha256).toHex();
    const QString path = directory + QLatin1Char('/') + QString::fromLatin1(name) + QStringLiteral(".png");
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(encoded) != encoded.size() || !file.commit()) return {};
    return QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
}

std::vector<QString> materializeCachedPreviews(PfCache* cache, const std::vector<std::string>& keys,
    const QString& directory, const std::function<bool()>& cancelled, std::size_t workers)
{
    std::vector<QString> urls(keys.size());
    if (!cache || keys.empty() || directory.isEmpty()) return urls;
    // Deduplicate before parallel writes, including callers with duplicate
    // timestamps. Independent files may be read/touched concurrently; no put,
    // eviction or clear occurs until this operation has joined every worker.
    std::unordered_map<std::string, std::size_t> first;
    std::vector<std::size_t> tasks, originals;
    originals.reserve(keys.size()); tasks.reserve(keys.size());
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const auto [entry, inserted] = first.try_emplace(keys[i], i);
        originals.push_back(entry->second);
        if (inserted) tasks.push_back(i);
    }
    workers = std::clamp<std::size_t>(workers, 1, std::min<std::size_t>(4, tasks.size()));
    std::atomic_size_t next{0};
    const auto work = [&] {
        for (;;) {
            if (cancelled && cancelled()) break;
            const auto task = next.fetch_add(1, std::memory_order_relaxed);
            if (task >= tasks.size()) break;
            const auto index = tasks[task];
            try { urls[index] = materializeCachedPreview(cache, keys[index], directory); }
            catch (...) { /* A bad cached still is a miss, not an analysis failure. */ }
        }
    };
    std::vector<std::jthread> pool;
    for (std::size_t i = 1; i < workers; ++i) pool.emplace_back(work);
    work();
    for (auto& thread : pool) thread.join();
    for (std::size_t i = 0; i < keys.size(); ++i)
        if (originals[i] != i) urls[i] = urls[originals[i]];
    return urls;
}
} // namespace pfservices
