#pragma once

#include <QNetworkReply>
#include <QTimer>
#include <stop_token>
#include <string>

namespace pfservices::detail {

// QNetworkReply must be aborted on its owning thread. Poll inside the
// existing worker event loop instead of calling Qt from a stop callback.
inline void watchNetworkCancellation(QTimer& timer, QNetworkReply& reply,
                                     std::stop_token stop, std::string& error)
{
    if (!stop.stop_possible()) return;
    QObject::connect(&timer, &QTimer::timeout, &reply, [&reply, stop, &error] {
        if (stop.stop_requested() && !reply.isFinished()) {
            error = "operation cancelled";
            reply.abort();
        }
    });
    timer.start(100);
}

inline void boundManifestReply(QNetworkReply& reply, qsizetype maximum,
                                std::string& error)
{
    // Leave one extra byte so an oversized response is rejected rather than
    // silently stalled at the cap. No unbounded response buffering.
    reply.setReadBufferSize(maximum + 1);
    QObject::connect(&reply, &QNetworkReply::readyRead, &reply, [&reply, maximum, &error] {
        if (reply.bytesAvailable() > maximum) {
            error = "manifest is too large";
            reply.abort();
        }
    });
}

} // namespace pfservices::detail
