#pragma once

#include <QNetworkReply>
#include <QThread>
#include <QTimer>
#include <string>
#include <QRegularExpression>

namespace pfservices::detail {
inline bool validFullResponseLength(qint64 length, std::uint64_t expectedSize) {
    return length < 0 || !expectedSize || static_cast<std::uint64_t>(length) == expectedSize;
}
inline bool withinDownloadLimit(std::uint64_t current, std::uint64_t incoming,
                                std::uint64_t expectedSize, std::uint64_t maximumSize) {
    const auto limit = expectedSize && expectedSize < maximumSize ? expectedSize : maximumSize;
    return current <= limit && incoming <= limit - current;
}
inline bool validContentRange(const QByteArray& header,std::uint64_t offset,
    std::uint64_t expectedSize,qint64 contentLength) {
    const auto match=QRegularExpression(QStringLiteral("^bytes ([0-9]+)-([0-9]+)/([0-9]+)$"))
        .match(QString::fromLatin1(header));
    if(!match.hasMatch())return false;
    bool a=false,b=false,c=false;
    const auto first=match.captured(1).toULongLong(&a),last=match.captured(2).toULongLong(&b),total=match.captured(3).toULongLong(&c);
    return a&&b&&c&&first==offset&&last>=first&&last<total
        &&(!expectedSize||total==expectedSize)
        &&(contentLength<0||static_cast<qulonglong>(contentLength)==last-first+1);
}
inline void watchInterruption(QTimer& timer, QNetworkReply& reply, std::string& error)
{
    auto* worker = QThread::currentThread();
    timer.setInterval(100);
    QObject::connect(&timer, &QTimer::timeout, &reply, [worker, &reply, &error] {
        if (worker->isInterruptionRequested()) {
            error = "download cancelled";
            reply.abort();
        }
    });
    timer.start();
}

// Manifests remain buffered until JSON parsing. Bound the reply itself,
// rather than checking its size only after a potentially unbounded transfer.
inline void boundManifestReply(QNetworkReply& reply, qsizetype limit,
                               std::string& error, const char* message)
{
    reply.setReadBufferSize(limit + 1);
    QObject::connect(&reply, &QNetworkReply::readyRead, &reply,
        [&reply, limit, &error, message] {
            if (reply.bytesAvailable() > limit) {
                error = message;
                reply.abort();
            }
        });
}
} // namespace pfservices::detail
