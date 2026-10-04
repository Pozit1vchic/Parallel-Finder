#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

namespace pfservices {

// Versioned disk cache used for intermediate pose/motion data. Every entry is
// self-describing (PFCACHE1) and written through a temporary file before the
// final rename, so interrupted analysis cannot leave a valid-looking entry.
// Entries live in root/ParallelFinder-cache. Verified legacy entries in root
// are moved there; unrelated files in the selected root are never removed.
class PfCache {
public:
    static constexpr std::size_t kDefaultLimitBytes = 8ULL * 1024ULL * 1024ULL * 1024ULL;

    explicit PfCache(std::filesystem::path root,
                     std::size_t limitBytes = kDefaultLimitBytes);

    [[nodiscard]] std::optional<std::vector<std::uint8_t>> get(const std::string& key,
        std::size_t maxPayloadBytes = 512ULL * 1024ULL * 1024ULL) const;
    bool put(const std::string& key, const std::vector<std::uint8_t>& payload,
             std::string& error);
    bool erase(const std::string& key, std::string& error);
    bool clear(std::string& error);
    [[nodiscard]] std::size_t bytesUsed() const;
    [[nodiscard]] std::size_t limitBytes() const noexcept { return limitBytes_; }

private:
    std::filesystem::path fileFor(const std::string& key) const;
    bool readEntry(const std::filesystem::path& path, const std::string& key,
                   std::vector<std::uint8_t>& payload, std::size_t maxPayloadBytes) const;
    bool writeEntry(const std::filesystem::path& path, const std::string& key,
                    const std::vector<std::uint8_t>& payload, std::string& error) const;
    void evictIfNeeded(const std::filesystem::path& protectedPath) const;

    std::filesystem::path root_;
    std::size_t limitBytes_;
    // Copies and independently opened instances for the same canonical root
    // coordinate mutations; concurrent readers retain shared access.
    std::shared_ptr<std::shared_mutex> access_;
};

} // namespace pfservices
