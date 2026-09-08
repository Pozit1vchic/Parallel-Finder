// QTest smoke for the static QML module PfUi: Main.qml loads from resources,
// Theme/L10n singletons resolve, AppInfo bridge is registered.
#include <QtGui/QColor>
#include <QtGui/QGuiApplication>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlComponent>
#include <QtQuick/QQuickWindow>
#include <QtTest/QTest>

#include <AppInfo.h>

class UiSmokeTests : public QObject {
    Q_OBJECT

private slots:
    void mainQmlLoadsFromResources();
    void themeSingletonResolves();
    void appInfoBridgeResolves();
};

void UiSmokeTests::mainQmlLoadsFromResources()
{
    pfui::AppInfo::registerQmlTypes();
    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/PfUi/qml/Main.qml")));
    QVERIFY2(!engine.rootObjects().isEmpty(), "PfUi.Main failed to load");
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
    QVERIFY(window != nullptr);
    QCOMPARE(window->color().name(), QStringLiteral("#262624"));
}

namespace {
QQuickWindow* loadWindow(QQmlApplicationEngine& engine)
{
    // See app/main.cpp: explicit import path + URL load for static-module
    // resources under shared Qt.
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/PfUi/qml/Main.qml")));
    return qobject_cast<QQuickWindow*>(engine.rootObjects().value(0, nullptr));
}
} // namespace

void UiSmokeTests::themeSingletonResolves()
{
    pfui::AppInfo::registerQmlTypes();
    QQmlApplicationEngine engine;
    if (!loadWindow(engine)) {
        QSKIP("Main.qml unavailable in this environment");
    }
    // Theme singleton must be reachable from an inline component.
    QQmlComponent component(&engine);
    component.setData(
        "import QtQuick\nimport PfUi\nimport PfUiBridge\n"
        "Item { property color bg: Theme.background\n"
        "       property real radius: Theme.radiusCard }",
        QUrl());
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    auto* item = component.create();
    QVERIFY(item != nullptr);
    QCOMPARE(item->property("bg").value<QColor>().name(), QStringLiteral("#262624"));
    QCOMPARE(item->property("radius").toReal(), 12.0);
    delete item;
}

void UiSmokeTests::appInfoBridgeResolves()
{
    pfui::AppInfo::registerQmlTypes();
    QQmlApplicationEngine engine;
    if (!loadWindow(engine)) {
        QSKIP("Main.qml unavailable in this environment");
    }
    QQmlComponent component(&engine);
    component.setData(
        "import QtQuick\nimport PfUiBridge\n"
        "Item { property string v: AppInfo.version\n"
        "       property string g: AppInfo.gpuBackend }",
        QUrl());
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    auto* item = component.create();
    QVERIFY(item != nullptr);
    const QString version = item->property("v").toString();
    const QString backend = item->property("g").toString();
    QVERIFY2(!version.isEmpty(), "AppInfo.version must not be empty");
    QCOMPARE(backend, QStringLiteral("cpu")); // stage 0: CPU-only stub
    delete item;
}

QTEST_MAIN(UiSmokeTests)
#include "test_ui_smoke.moc"
