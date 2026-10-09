#include "DiscordPresence.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUuid>
#include <QtEndian>
#include <algorithm>
#include <cmath>

namespace pfui {
namespace { constexpr quint32 MaxPayload = 64 * 1024; }
DiscordPresence::DiscordPresence(QObject* parent, QString endpoint)
    : QObject(parent), endpoint_(std::move(endpoint)) {
    reconnect_.setSingleShot(true); deadline_.setSingleShot(true); publishTimer_.setSingleShot(true);
    socket_.setReadBufferSize(MaxPayload + 8);
    connect(&reconnect_, &QTimer::timeout, this, &DiscordPresence::connectNext);
    connect(&deadline_, &QTimer::timeout, this, &DiscordPresence::retry);
    connect(&publishTimer_, &QTimer::timeout, this, &DiscordPresence::publish);
    connect(&socket_, &QLocalSocket::connected, this, [this] {
        writeFrame(0, QJsonDocument(QJsonObject{{"v", 1}, {"client_id", "1558099632775766167"}}).toJson(QJsonDocument::Compact));
        deadline_.start(5000);
    });
    connect(&socket_, &QLocalSocket::readyRead, this, &DiscordPresence::readFrames);
    connect(&socket_, &QLocalSocket::disconnected, this, &DiscordPresence::retry);
    connect(&socket_, &QLocalSocket::errorOccurred, this, [this](auto) { retry(); });
}
QByteArray DiscordPresence::frame(quint32 opcode, const QByteArray& payload) {
    QByteArray bytes(8, '\0');
    qToLittleEndian(opcode, bytes.data());
    qToLittleEndian(static_cast<quint32>(payload.size()), bytes.data() + 4);
    bytes += payload; return bytes;
}
void DiscordPresence::setState(const QString& value) {
    if (state_ == value) return;
    state_ = value; emit connectionStateChanged();
}
void DiscordPresence::start() {
    if (started_) return;
    started_ = true;
    reconnect_.start(0);
}
void DiscordPresence::stop() {
    started_ = false;
    reconnect_.stop(); deadline_.stop(); publishTimer_.stop();
    if (ready_) {
        QJsonObject command{{"cmd", "SET_ACTIVITY"}, {"nonce", QUuid::createUuid().toString(QUuid::WithoutBraces)},
            {"args", QJsonObject{{"pid", QCoreApplication::applicationPid()}, {"activity", QJsonValue::Null}}}};
        writeFrame(1, QJsonDocument(command).toJson(QJsonDocument::Compact));
        socket_.flush();
    }
    ready_ = false; pendingNonce_.clear(); input_.clear(); lastSent_ = {};
    socket_.disconnectFromServer();
    setState("waiting");
}
void DiscordPresence::connectNext() {
    if (!started_) return;
    ready_ = false; input_.clear(); pendingNonce_.clear(); lastSent_ = {}; sentAt_.invalidate();
    socket_.abort();
    const QString path = endpoint_.isEmpty()
        ? QStringLiteral("discord-ipc-%1").arg(slot_++ % 10) : endpoint_;
    setState("connecting"); deadline_.start(2000);
    socket_.connectToServer(path);
}
void DiscordPresence::retry() {
    deadline_.stop(); ready_ = false; pendingNonce_.clear(); input_.clear(); publishTimer_.stop();
    if (!started_) return;
    // One retry timer prevents error+disconnected from creating duplicate jobs.
    if (!reconnect_.isActive()) reconnect_.start(slot_ % 10 == 0 ? 15000 : 250);
    setState("waiting");
}
void DiscordPresence::writeFrame(quint32 opcode, const QByteArray& payload) {
    if (socket_.state() != QLocalSocket::ConnectedState || socket_.bytesToWrite() > MaxPayload) return;
    socket_.write(frame(opcode, payload));
}
void DiscordPresence::readFrames() {
    input_ += socket_.readAll();
    if (input_.size() > MaxPayload + 8) { socket_.abort(); retry(); return; }
    while (input_.size() >= 8) {
        const auto opcode = qFromLittleEndian<quint32>(input_.constData());
        const auto size = qFromLittleEndian<quint32>(input_.constData() + 4);
        if (size > MaxPayload || opcode > 4) { socket_.abort(); retry(); return; }
        if (input_.size() < 8 + static_cast<qsizetype>(size)) return;
        const auto payload = input_.mid(8, size); input_.remove(0, 8 + size);
        if (opcode == 3) { writeFrame(4, payload); continue; }
        if (opcode == 4) continue;
        if (opcode == 2) { socket_.abort(); retry(); return; }
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(payload, &error);
        if (opcode != 1 || error.error != QJsonParseError::NoError || !doc.isObject()) { socket_.abort(); retry(); return; }
        const auto object = doc.object();
        if (!ready_ && object.value("evt").toString() == "READY" && object.value("cmd").toString() == "DISPATCH") {
            ready_ = true; deadline_.stop(); setState("connected"); publish();
        } else if (!pendingNonce_.isEmpty() && object.value("nonce").toString() == pendingNonce_) {
            pendingNonce_.clear(); deadline_.stop();
            if (object.value("evt").toString() == "ERROR") {
                lastSent_ = {}; socket_.abort();
                setState("error"); reconnect_.start(30000); return;
            }
            setState("connected"); publish();
        }
    }
}
void DiscordPresence::setActivity(bool analyzing, bool exporting, double progress, int results,
                                 const QString& stage, const QString& language) {
    const bool ru = language == "ru";
    const QString phase = exporting ? "export" : analyzing ? (stage.startsWith(QStringLiteral("Матчер:")) ? "match" : "analyze") : results > 0 ? "results" : "idle";
    if (phase != phase_) { phase_ = phase; phaseStart_ = QDateTime::currentSecsSinceEpoch(); }
    QString details = phase == "export" ? (ru ? "Экспортирует параллели" : "Exporting parallels")
        : phase == "match" ? (ru ? "Ищет параллели" : "Finding parallels")
        : phase == "analyze" ? (ru ? "Анализирует видео" : "Analyzing videos")
        : phase == "results" ? (ru ? "Просматривает результаты" : "Reviewing results")
        : (ru ? "Ожидает видео" : "Waiting for videos");
    const int percent = std::isfinite(progress) ? static_cast<int>(std::clamp(progress, 0.0, 1.0) * 100) : 0;
    const QString state = analyzing && !exporting ? QString::number(percent) + "%"
        : ru ? QStringLiteral("Найдено пар: %1").arg(std::max(results, 0)) : QStringLiteral("Pairs found: %1").arg(std::max(results, 0));
    desired_ = {{"type", 0}, {"details", details}, {"state", state},
        {"timestamps", QJsonObject{{"start", phaseStart_}}},
        {"assets", QJsonObject{{"large_image", "parallel_finder"}, {"large_text", "Parallel Finder"}}},
        {"buttons", QJsonArray{QJsonObject{{"label", ru ? "Скачать Parallel Finder" : "Get Parallel Finder"},
            {"url", "https://github.com/Pozit1vchic/Parallel-Finder/releases/latest"}}}}};
    publish();
}
void DiscordPresence::publish() {
    if (!ready_ || !started_ || !pendingNonce_.isEmpty() || desired_.isEmpty() || desired_ == lastSent_) return;
    if (sentAt_.isValid() && sentAt_.elapsed() < 15000) {
        publishTimer_.start(static_cast<int>(15000 - sentAt_.elapsed())); return;
    }
    pendingNonce_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QJsonObject command{{"cmd", "SET_ACTIVITY"}, {"nonce", pendingNonce_},
        {"args", QJsonObject{{"pid", QCoreApplication::applicationPid()}, {"activity", desired_}}}};
    writeFrame(1, QJsonDocument(command).toJson(QJsonDocument::Compact));
    lastSent_ = desired_; sentAt_.restart(); deadline_.start(10000);
}
}
