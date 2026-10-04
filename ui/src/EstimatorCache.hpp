#pragma once

#include <algorithm>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace pfui {

// Retain the current and previous configuration, rather than every model,
// provider and thread count ever selected. In-flight callers own their copy.
template<class Estimator>
class EstimatorCache {
public:
    explicit EstimatorCache(std::size_t capacity = 2) : capacity_(capacity)
    {
        if (capacity == 0) throw std::invalid_argument("EstimatorCache: empty capacity");
        entries_.reserve(capacity);
    }

    template<class Factory>
    std::shared_ptr<Estimator> getOrCreate(const std::string& key, Factory&& factory)
    {
        std::shared_ptr<Estimator> retired, result;
        {
            const std::lock_guard lock(mutex_);
            const auto found = std::find_if(entries_.begin(), entries_.end(),
                [&](const auto& item) { return item.key == key; });
            if (found != entries_.end()) {
                result = found->estimator;
                std::rotate(found, found + 1, entries_.end());
            } else {
                // Factories only construct estimators, never prepare/infer.
                // Complete allocations before modifying existing retention.
                Entry incoming{key, std::forward<Factory>(factory)()};
                if (!incoming.estimator) throw std::invalid_argument("EstimatorCache: null estimator");
                result = incoming.estimator;
                if (entries_.size() == capacity_) {
                    retired = std::move(entries_.front().estimator);
                    entries_.erase(entries_.begin());
                }
                entries_.push_back(std::move(incoming));
            }
        }
        // Destruction of a large workspace/provider resource is outside the
        // cache lock. An active caller may still hold the evicted estimator.
        return result;
    }

    std::size_t size() const
    {
        const std::lock_guard lock(mutex_);
        return entries_.size();
    }

private:
    struct Entry { std::string key; std::shared_ptr<Estimator> estimator; };
    const std::size_t capacity_;
    mutable std::mutex mutex_;
    std::vector<Entry> entries_;
};

} // namespace pfui
