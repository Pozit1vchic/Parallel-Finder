#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pfservices {

// In-memory LRU cache for decoded thumbnail bytes.  Disk persistence and the
// configurable cache root are deliberately separate so a failed disk write
// can never invalidate a usable preview already shown by the UI.
class ThumbnailCache {
public:
    // "LRU64" — default capacity of 64 entries.
    static constexpr std::size_t kDefaultMaxEntries = 64;

    explicit ThumbnailCache(std::size_t maxEntries = kDefaultMaxEntries);

    // Capacity must be >= 1. Throws std::invalid_argument otherwise.
    void setMaxEntries(std::size_t maxEntries);
    std::size_t maxEntries() const noexcept;

    using Bytes = std::vector<std::uint8_t>;

    void put(std::string key, Bytes bytes);
    std::optional<Bytes> get(const std::string& key);
    bool erase(const std::string& key);
    void clear() noexcept;
    std::size_t size() const noexcept;

private:
    struct Entry {
        std::string key;
        Bytes bytes;
    };

    using List = std::list<Entry>;
    using Index = std::unordered_map<std::string, List::iterator>;

    mutable std::mutex mutex_;
    std::size_t maxEntries_;
    List entries_;
    Index index_;
};

} // namespace pfservices
