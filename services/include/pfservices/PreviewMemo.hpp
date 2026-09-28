#pragma once

#include <QString>
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <utility>

namespace pfservices {

// One analysis owns one memo. It stores output URLs, not decoded frame buffers;
// no stale entries survive changes to sources between analysis runs.
class PreviewMemo {
public:
    using Renderer = std::function<QString(const QString&, double)>;
    explicit PreviewMemo(Renderer renderer) : renderer_(std::move(renderer)) {}

    QString get(const QString& source, double seconds)
    {
        if (!std::isfinite(seconds)) return {};
        const auto key = std::make_pair(source, std::max(0.0, seconds));
        if (const auto found = urls_.find(key); found != urls_.end()) return found->second;
        QString url = renderer_(key.first, key.second);
        if (!url.isEmpty()) urls_.emplace(key, url);
        return url;
    }

private:
    Renderer renderer_;
    std::map<std::pair<QString, double>, QString> urls_;
};

} // namespace pfservices
