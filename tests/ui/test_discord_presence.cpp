#include "DiscordPresence.h"
#include <pfservices/SettingsStore.hpp>
#include <QtTest>
#include <QLocalServer>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QtEndian>
#include <memory>

class DiscordTests : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setApplicationName("ParallelFinderDiscordTests");
    }
    void protocolAndPrivacy() {
        QLocalServer server;
        const QString name = "pf-discord-test-" + QString::number(QCoreApplication::applicationPid());
        QVERIFY(server.listen(name));
        pfui::DiscordPresence presence(nullptr, name);
        presence.setActivity(true, false, .42, 7, QStringLiteral("Матчер: Сравниваем"), "ru");
        presence.start();
        QTRY_VERIFY(server.hasPendingConnections());
        std::unique_ptr<QLocalSocket> peer(server.nextPendingConnection());
        QTRY_VERIFY(peer->bytesAvailable() > 8);
        const auto hello = peer->readAll();
        QCOMPARE(qFromLittleEndian<quint32>(hello.constData()), 0u);
        QCOMPARE(QJsonDocument::fromJson(hello.mid(8)).object().value("client_id").toString(), QString("1558099632775766167"));
        const auto ready = pfui::DiscordPresence::frame(1, R"({"cmd":"DISPATCH","evt":"READY"})");
        // Header and payload may arrive in separate pipe reads.
        peer->write(ready.left(5)); peer->flush(); QTest::qWait(20);
        QVERIFY(presence.connectionState() != "connected");
        peer->write(ready.mid(5)); peer->flush();
        QTRY_COMPARE(presence.connectionState(), QString("connected"));
        QTRY_VERIFY(peer->bytesAvailable() > 8);
        const auto activity = QJsonDocument::fromJson(peer->readAll().mid(8)).object();
        QCOMPARE(activity.value("cmd").toString(), QString("SET_ACTIVITY"));
        const auto data = activity.value("args").toObject().value("activity").toObject();
        QCOMPARE(data.value("details").toString(), QStringLiteral("Ищет параллели"));
        QCOMPARE(data.value("state").toString(), QString("42%"));
        QCOMPARE(data.value("assets").toObject().value("large_image").toString(), QString("parallel_finder"));
        QVERIFY(!data.contains("files"));
        peer->write(pfui::DiscordPresence::frame(1, QJsonDocument(QJsonObject{
            {"cmd", "SET_ACTIVITY"}, {"nonce", activity.value("nonce")}, {"evt", QJsonValue::Null}}).toJson(QJsonDocument::Compact)));
        peer->flush(); QTest::qWait(30);
        presence.setActivity(false, true, 0, 7, {}, "en");
        QTest::qWait(30);
        QCOMPARE(peer->bytesAvailable(), qint64(0)); // coalesced, rate-limited update
        peer->write(pfui::DiscordPresence::frame(3, "ping")); peer->flush();
        QTRY_VERIFY(peer->bytesAvailable() >= 12);
        const auto pong = peer->readAll();
        QCOMPARE(qFromLittleEndian<quint32>(pong.constData()), 4u);
        QCOMPARE(pong.mid(8), QByteArray("ping"));
        presence.stop();
        QTRY_VERIFY(peer->bytesAvailable() > 8);
        const auto clear = QJsonDocument::fromJson(peer->readAll().mid(8)).object();
        QVERIFY(clear.value("args").toObject().value("activity").isNull());
        QCOMPARE(presence.connectionState(), QString("waiting"));
        pfservices::SettingsStore::flushPendingWrites();
        std::string error;
        QVERIFY(error.empty());
    }
    void invalidFrameAndReconnect() {
        QLocalServer server;
        const QString name = "pf-discord-bad-" + QString::number(QCoreApplication::applicationPid());
        QVERIFY(server.listen(name));
        pfui::DiscordPresence presence(nullptr, name);
        presence.start();
        QTRY_VERIFY(server.hasPendingConnections());
        std::unique_ptr<QLocalSocket> peer(server.nextPendingConnection());
        QByteArray oversized(8, '\0'); qToLittleEndian(1u, oversized.data());
        qToLittleEndian(1000000u, oversized.data() + 4);
        peer->write(oversized); peer->flush();
        QTRY_COMPARE(presence.connectionState(), QString("waiting"));
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingConnections(), 17000);
        presence.stop();
    }
    void missingDiscordDoesNotBlock() {
        pfui::DiscordPresence presence(nullptr, "pf-discord-missing-endpoint");
        QElapsedTimer timer; timer.start(); presence.start();
        QVERIFY(timer.elapsed() < 100);
        QTRY_COMPARE(presence.connectionState(), QString("waiting"));
        presence.stop();
    }
    void exportAndDiscordRejection() {
        QLocalServer server;
        const QString name = "pf-discord-error-" + QString::number(QCoreApplication::applicationPid());
        QVERIFY(server.listen(name));
        pfui::DiscordPresence presence(nullptr, name);
        presence.setActivity(false, true, 0, 12, {}, "en");
        presence.start();
        QTRY_VERIFY(server.hasPendingConnections());
        std::unique_ptr<QLocalSocket> peer(server.nextPendingConnection());
        QTRY_VERIFY(peer->bytesAvailable() > 8); peer->readAll();
        peer->write(pfui::DiscordPresence::frame(1, R"({"cmd":"DISPATCH","evt":"READY"})")); peer->flush();
        QTRY_VERIFY(peer->bytesAvailable() > 8);
        const auto command = QJsonDocument::fromJson(peer->readAll().mid(8)).object();
        const auto activity = command.value("args").toObject().value("activity").toObject();
        QCOMPARE(activity.value("details").toString(), QString("Exporting parallels"));
        QCOMPARE(activity.value("state").toString(), QString("Pairs found: 12"));
        peer->write(pfui::DiscordPresence::frame(1, QJsonDocument(QJsonObject{
            {"cmd", "SET_ACTIVITY"}, {"nonce", command.value("nonce")}, {"evt", "ERROR"},
            {"data", QJsonObject{{"code", 4000}, {"message", "test rejection"}}}}).toJson(QJsonDocument::Compact)));
        peer->flush();
        QTRY_COMPARE(presence.connectionState(), QString("error"));
        presence.stop();
    }
};
QTEST_GUILESS_MAIN(DiscordTests)
#include "test_discord_presence.moc"
