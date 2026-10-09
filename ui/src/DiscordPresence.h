#pragma once
#include <QObject>
#include <QLocalSocket>
#include <QTimer>
#include <QElapsedTimer>
#include <QJsonObject>

namespace pfui {
// Discord's local IPC protocol. No account tokens or remote HTTP requests.
class DiscordPresence final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString connectionState READ connectionState NOTIFY connectionStateChanged)
public:
    explicit DiscordPresence(QObject* parent = nullptr, QString endpoint = {});
    QString connectionState() const { return state_; }
    void start();
    void stop();
    void setActivity(bool analyzing, bool exporting, double progress, int results,
                     const QString& stage, const QString& language);
    static QByteArray frame(quint32 opcode, const QByteArray& payload);
signals:
    void connectionStateChanged();
private:
    void connectNext();
    void retry();
    void readFrames();
    void publish();
    void setState(const QString& value);
    void writeFrame(quint32 opcode, const QByteArray& payload);
    QLocalSocket socket_;
    QTimer reconnect_, deadline_, publishTimer_;
    QElapsedTimer sentAt_;
    QString endpoint_, state_ = QStringLiteral("waiting"), pendingNonce_, phase_;
    QByteArray input_;
    QJsonObject desired_, lastSent_;
    qint64 phaseStart_ = 0;
    int slot_ = 0;
    bool started_ = false, ready_ = false;
};
}
