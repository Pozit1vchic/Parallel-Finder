#pragma once

#include "pfservices/PfCache.hpp"
#include <QCryptographicHash>
#include <QFile>
#include <QImage>
#include <QSaveFile>
#include <QUrl>
#include <functional>

namespace pfservices {
// Encoded frames share the normal bounded cache, but never reference a prior
// temporary session. A hit is materialized into this session's own directory.
inline QString cachedPreview(PfCache* cache, const std::string& key,
                             const QString& directory,
                             const std::function<QString()>& render, bool& hit)
{
    hit = false;
    if (cache && !directory.isEmpty()) {
        if (const auto bytes = cache->get(key); bytes && !bytes->empty()) {
            const QByteArray encoded(reinterpret_cast<const char*>(bytes->data()), static_cast<qsizetype>(bytes->size()));
            if (!QImage::fromData(encoded, "PNG").isNull()) {
                const auto name = QCryptographicHash::hash(QByteArray::fromStdString(key), QCryptographicHash::Sha256).toHex();
                const QString path = directory + QLatin1Char('/') + QString::fromLatin1(name) + QStringLiteral(".png");
                QSaveFile file(path);
                if (file.open(QIODevice::WriteOnly) && file.write(encoded) == encoded.size() && file.commit()) {
                    hit = true;
                    return QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
                }
            }
        }
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
