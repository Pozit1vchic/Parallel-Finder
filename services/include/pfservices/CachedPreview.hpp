#pragma once

#include "pfservices/PfCache.hpp"
#include <QCryptographicHash>
#include <QFile>
#include <QImage>
#include <QSaveFile>
#include <QUrl>
#include <functional>
#include <vector>

namespace pfservices {
// A miss, corrupt PNG or failed materialization returns an empty URL. Pixel
// validation is deliberately retained, even on the parallel cache path.
QString materializeCachedPreview(PfCache* cache, const std::string& key, const QString& directory);
std::vector<QString> materializeCachedPreviews(PfCache* cache, const std::vector<std::string>& keys,
    const QString& directory, const std::function<bool()>& cancelled = {}, std::size_t workers = 4);
// Encoded frames share the normal bounded cache, but never reference a prior
// temporary session. A hit is materialized into this session's own directory.
inline QString cachedPreview(PfCache* cache, const std::string& key,
                             const QString& directory,
                             const std::function<QString()>& render, bool& hit)
{
    hit = false;
    if (const auto url = materializeCachedPreview(cache, key, directory); !url.isEmpty()) {
        hit = true;
        return url;
    }
    const auto url = render();
    if (cache && !url.isEmpty()) {
        QFile file(QUrl(url).toLocalFile());
        if (file.open(QIODevice::ReadOnly)) {
            const auto bytes = file.readAll();
            std::string error;
            cache->put(key, std::vector<std::uint8_t>(bytes.begin(), bytes.end()), error);
        }
    }
    return url;
}
} // namespace pfservices
