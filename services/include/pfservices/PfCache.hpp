#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pfservices {

// Versioned disk cache used for intermediate pose/motion data. Every entry is
// self-describing (PFCACHE1) and written through a temporary file before the
// final rename, so interrupted analysis cannot leave a valid-looking entry.
class PfCache {
public:
    static constexpr std::size_t kDefaultLimitBytes = 8ULL * 1024ULL * 1024ULL * 1024ULL;

    explicit PfCache(std::filesystem::path root,
                     std::size_t limitBytes = kDefaultLimitBytes);

    [[nodiscard]] std::optional<std::vector<std::uint8_t>> get(const std::string& key) const;
    bool put(const std::string& key, const std::vector<std::uint8_t>& payload,
             std::string& error);
    bool erase(const std::string& key, std::string& error);
    // Ownership is kept separately from opaque hashed cache keys.
    bool rememberSourceKey(const std::string& source, const std::string& key, std::string& error);
    bool putForSource(const std::string& source, const std::string& key,
                      const std::vector<std::uint8_t>& payload, std::string& error) {
        return rememberSourceKey(source,key,error) && put(key,payload,error);
    }
    [[nodiscard]] std::vector<std::string> keysForSource(const std::string& source, std::string* error = nullptr) const;
    // Persistent per-source namespace, outside LRU eviction: resetting a
    // video must never make its older measurements reusable later.
    [[nodiscard]] std::string sourceGeneration(const std::string& source) const;
    bool resetSource(const std::string& source, std::string& error);
    bool clear(std::string& error);
    [[nodiscard]] std::size_t bytesUsed() const;
    [[nodiscard]] std::size_t limitBytes() const noexcept { return limitBytes_; }

private:
    std::filesystem::path fileFor(const std::string& key) const;
    bool readEntry(const std::filesystem::path& path, const std::string& key,
                   std::vector<std::uint8_t>& payload) const;
    bool writeEntry(const std::filesystem::path& path, const std::string& key,
                    const std::vector<std::uint8_t>& payload, std::string& error) const;
    void evictIfNeeded(const std::filesystem::path& protectedPath) const;

    std::filesystem::path root_;
    std::size_t limitBytes_;
};

} // namespace pfservices
