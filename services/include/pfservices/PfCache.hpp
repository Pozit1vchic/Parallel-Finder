#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace pfservices {

// A flat, explicit set of managed cache directories shares one capacity and
// one in-process coordination lock. Never discovers or traverses subfolders.
class CacheBudget {
public:
    CacheBudget(std::vector<std::filesystem::path> roots, std::size_t limitBytes);
    bool enforceLimit(std::string& error) const;
    [[nodiscard]] std::size_t bytesUsed() const;
private:
    friend class PfCache;
    bool enforce(const std::filesystem::path& protectedPath, std::string& error) const;
    std::vector<std::filesystem::path> roots_;
    std::size_t limitBytes_;
    mutable std::recursive_mutex mutex_;
};

// Versioned disk cache used for intermediate pose/motion data. Every entry is
// self-describing (PFCACHE1) and written through a temporary file before the
// final rename, so interrupted analysis cannot leave a valid-looking entry.
class PfCache {
public:
    static constexpr std::size_t kDefaultLimitBytes = 8ULL * 1024ULL * 1024ULL * 1024ULL;

    explicit PfCache(std::filesystem::path root,
                     std::size_t limitBytes = kDefaultLimitBytes,
                     std::shared_ptr<CacheBudget> budget = {});

    [[nodiscard]] std::optional<std::vector<std::uint8_t>> get(const std::string& key) const;
    bool put(const std::string& key, const std::vector<std::uint8_t>& payload,
             std::string& error);
    // Write a bounded group of independent entries before one LRU scan.
    // Successfully written entries are still budgeted if a later write fails.
    bool putBatch(const std::vector<std::pair<std::string,std::vector<std::uint8_t>>>& entries,
                  std::string& error);
    bool erase(const std::string& key, std::string& error);
    // Ownership is kept separately from opaque hashed cache keys.
    bool rememberSourceKey(const std::string& source, const std::string& key, std::string& error);
    bool putForSource(const std::string& source, const std::string& key,
                      const std::vector<std::uint8_t>& payload, std::string& error);
    // Write a group of observations for one source, then perform one LRU
    // budget scan instead of rescanning the growing cache after every entry.
    bool putForSourceBatch(const std::string& source,
                           const std::vector<std::pair<std::string,std::vector<std::uint8_t>>>& entries,
                           std::string& error);
    [[nodiscard]] std::vector<std::string> keysForSource(const std::string& source, std::string* error = nullptr) const;
    // Persistent per-source namespace, outside LRU eviction: resetting a
    // video must never make its older measurements reusable later.
    [[nodiscard]] std::string sourceGeneration(const std::string& source) const;
    bool resetSource(const std::string& source, std::string& error);
    bool clear(std::string& error);
    [[nodiscard]] std::size_t bytesUsed() const;
    [[nodiscard]] std::size_t limitBytes() const noexcept { return limitBytes_; }
    [[nodiscard]] const std::filesystem::path& rootPath() const noexcept { return root_; }
    // Reapply a changed capacity even when a warm analysis writes nothing.
    bool enforceLimit(std::string& error) const;

private:
    std::filesystem::path fileFor(const std::string& key) const;
    bool readEntry(const std::filesystem::path& path, const std::string& key,
                   std::vector<std::uint8_t>& payload) const;
    bool writeEntry(const std::filesystem::path& path, const std::string& key,
                    const std::vector<std::uint8_t>& payload, std::string& error) const;
    bool evictIfNeeded(const std::filesystem::path& protectedPath, std::string& error) const;
    std::recursive_mutex& coordinationMutex() const;

    std::filesystem::path root_;
    std::size_t limitBytes_;
    std::size_t coordinationSlot_=0;
    std::shared_ptr<CacheBudget> budget_;
};

} // namespace pfservices
