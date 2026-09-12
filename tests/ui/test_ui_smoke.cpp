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
    void gpuInfoPropagatesToQml();
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
    QCOMPARE(window->color().name(), QStringLiteral("#0c0d0b"));
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
    QCOMPARE(item->property("bg").value<QColor>().name(), QStringLiteral("#0c0d0b"));
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
    QVERIFY2(!backend.isEmpty(), "AppInfo.gpuBackend must not be empty");
    delete item;
}

// The app's init step pushes the pfgpu probe result through setGpuInfo; QML
// must see exactly that, including the display summary the badge renders.
void UiSmokeTests::gpuInfoPropagatesToQml()
{
    pfui::AppInfo::instance()->setGpuInfo(QStringLiteral("cuda"),
                                          QStringLiteral("NVIDIA GeForce RTX 4070"), true,
                                          QStringLiteral("1.26.0"));
    pfui::AppInfo::registerQmlTypes();
    QQmlApplicationEngine engine;
    if (!loadWindow(engine)) {
        QSKIP("Main.qml unavailable in this environment");
    }
    QQmlComponent component(&engine);
    component.setData(
        "import QtQuick\nimport PfUiBridge\n"
        "Item { property string s: AppInfo.gpuSummary\n"
        "       property string d: AppInfo.gpuDevice\n"
        "       property bool gpu: AppInfo.backendIsGpu\n"
        "       property string ort: AppInfo.ortVersion }\n",
        QUrl());
    QVERIFY2(!component.isError(), qPrintable(component.errorString()));
    auto* item = component.create();
    QVERIFY(item != nullptr);
    QVERIFY(item->property("gpu").toBool());
    QCOMPARE(item->property("d").toString(), QStringLiteral("NVIDIA GeForce RTX 4070"));
    QCOMPARE(item->property("ort").toString(), QStringLiteral("1.26.0"));
    const QString summary = item->property("s").toString();
    QVERIFY2(summary.startsWith(QStringLiteral("cuda")), qPrintable(summary));
    QVERIFY2(summary.contains(QStringLiteral("4070")), qPrintable(summary));
    delete item;

    // Leave the singleton neutral for any later test.
    pfui::AppInfo::instance()->setGpuInfo(QStringLiteral("cpu"), QString(), false, QString());
}

QTEST_MAIN(UiSmokeTests)
#include "test_ui_smoke.moc"
