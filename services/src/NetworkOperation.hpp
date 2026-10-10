#pragma once

#include <QNetworkReply>
#include <QThread>
#include <QTimer>
#include <string>

namespace pfservices::detail {
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
