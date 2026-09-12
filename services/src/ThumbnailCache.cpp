#include "pfservices/ThumbnailCache.hpp"

#include <stdexcept>

namespace pfservices {

ThumbnailCache::ThumbnailCache(std::size_t maxEntries)
    : maxEntries_(maxEntries)
{
    if (maxEntries_ == 0) {
        throw std::invalid_argument("ThumbnailCache: capacity must be >= 1");
    }
}

void ThumbnailCache::setMaxEntries(std::size_t maxEntries)
{
    if (maxEntries == 0) {
        throw std::invalid_argument("ThumbnailCache: capacity must be >= 1");
    }
    std::lock_guard lock(mutex_);
    maxEntries_ = maxEntries;
    while (entries_.size() > maxEntries_) {
        index_.erase(entries_.back().key);
        entries_.pop_back();
    }
}

std::size_t ThumbnailCache::maxEntries() const noexcept
{
    std::lock_guard lock(mutex_);
    return maxEntries_;
}

void ThumbnailCache::put(std::string key, Bytes bytes)
{
    if (key.empty()) return;
    std::lock_guard lock(mutex_);
    if (const auto found = index_.find(key); found != index_.end()) {
        found->second->bytes = std::move(bytes);
        entries_.splice(entries_.begin(), entries_, found->second);
        found->second = entries_.begin();
        return;
    }
    entries_.push_front(Entry {std::move(key), std::move(bytes)});
    index_[entries_.front().key] = entries_.begin();
    while (entries_.size() > maxEntries_) {
        index_.erase(entries_.back().key);
        entries_.pop_back();
    }
}

std::optional<ThumbnailCache::Bytes> ThumbnailCache::get(const std::string& key)
{
    std::lock_guard lock(mutex_);
    const auto found = index_.find(key);
    if (found == index_.end()) return std::nullopt;
    entries_.splice(entries_.begin(), entries_, found->second);
    found->second = entries_.begin();
    return entries_.front().bytes;
}

bool ThumbnailCache::erase(const std::string& key)
{
    std::lock_guard lock(mutex_);
    const auto found = index_.find(key);
    if (found == index_.end()) return false;
    entries_.erase(found->second);
    index_.erase(found);
    return true;
}

void ThumbnailCache::clear() noexcept
{
    std::lock_guard lock(mutex_);
    index_.clear();
    entries_.clear();
}

std::size_t ThumbnailCache::size() const noexcept
{
    std::lock_guard lock(mutex_);
    return entries_.size();
}

} // namespace pfservices
