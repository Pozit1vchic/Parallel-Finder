// QTest smoke for the static QML module PfUi: Main.qml loads from resources,
// Theme/L10n singletons resolve, AppInfo bridge is registered.
#include <QtGui/QColor>
#include <QtGui/QGuiApplication>
#include <QtGui/QFontMetricsF>
#include <QtCore/QDir>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlComponent>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QQuickItem>
#include <QtTest/QTest>

#include <AppInfo.h>
#include <pfservices/SettingsStore.hpp>
#include <QFileInfo>

class UiSmokeTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { QCoreApplication::setOrganizationName("ParallelFinderTests"); QCoreApplication::setOrganizationDomain("test.invalid"); QCoreApplication::setApplicationName("UiAudit"); }
    void mainQmlLoadsFromResources();
    void themeSingletonResolves();
    void appInfoBridgeResolves();
    void gpuInfoPropagatesToQml();
    void settingsAndNumericTypography();
    void resultNavigationWrapsAndScrolls();
    void exportModesAreSelectable();
    void advancedOpensOnFirstClickAndStatusTranslates();
    void directMlDownloadIntegration();
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
    QCOMPARE(window->color().name(), QStringLiteral("#0c0d10"));
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
    QCOMPARE(item->property("bg").value<QColor>().name(), QStringLiteral("#0c0d10"));
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
    QVERIFY2(summary.startsWith(QStringLiteral("CUDA")), qPrintable(summary));
    QVERIFY2(summary.contains(QStringLiteral("4070")), qPrintable(summary));
    delete item;

    // Leave the singleton neutral for any later test.
    pfui::AppInfo::instance()->setGpuInfo(QStringLiteral("cpu"), QString(), false, QString());
}

void UiSmokeTests::settingsAndNumericTypography()
{
    pfui::AppInfo::registerQmlTypes();
    QQmlApplicationEngine engine;
    QStringList warnings;
    connect(&engine, &QQmlEngine::warnings, this, [&](const QList<QQmlError>& errors) {
        for (const auto& error : errors) warnings << error.toString();
    });
    auto* window = loadWindow(engine);
    QVERIFY(window);
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nimport PfUi\nItem { property string numericFamily: Theme.monoFont }", QUrl());
    std::unique_ptr<QObject> probe(component.create());
    QVERIFY(probe);
    QTRY_VERIFY(!probe->property("numericFamily").toString().isEmpty());
    QCOMPARE(probe->property("numericFamily").toString(), QStringLiteral("JetBrains Mono"));
    QFont font(probe->property("numericFamily").toString()); font.setPixelSize(18);
    QFontMetricsF metrics(font);
    QCOMPARE(metrics.horizontalAdvance("11111"), metrics.horizontalAdvance("88888"));
    const auto capture = qEnvironmentVariable("PF_UI_CAPTURE_DIR");
    if (!capture.isEmpty()) { QDir().mkpath(capture); QTest::qWait(250); QVERIFY(window->grabWindow().save(capture + "/workspace.png")); }
    auto* popup = window->findChild<QObject*>("settingsDialog");
    QVERIFY(popup);
    auto* sources = window->findChild<QObject*>("sourcesRail");
    QVERIFY(sources);
    QVERIFY(sources->findChild<QObject*>("costumeModeCheck"));
    QVERIFY(!popup->findChild<QObject*>("costumeModeCheck"));
    QVERIFY(QMetaObject::invokeMethod(popup, "open"));
    QTRY_VERIFY(popup->property("opened").toBool());
    if (!capture.isEmpty()) { QTest::qWait(300); QVERIFY(window->grabWindow().save(capture + "/settings-analysis.png")); }
    auto* tabs = popup->findChild<QObject*>("settingsTabs");
    QVERIFY(tabs); tabs->setProperty("currentIndex", 1);
    QTest::qWait(100);
    if (!capture.isEmpty()) QVERIFY(window->grabWindow().save(capture + "/settings-appearance.png"));
    QVERIFY(QMetaObject::invokeMethod(popup, "close"));
    QTRY_VERIFY(!popup->property("visible").toBool());
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
}

void UiSmokeTests::resultNavigationWrapsAndScrolls()
{
    pfui::AppInfo::registerQmlTypes();
    QQmlApplicationEngine engine;
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nimport PfUi\nResultsRail { width: 280; height: 500; results: [{id: 1, similarity: 0.8}, {id: 2, similarity: 0.9}]; onResultSelected: function(index) { selectedIndex = index } }", QUrl());
    std::unique_ptr<QObject> rail(component.create());
    QVERIFY2(rail != nullptr, qPrintable(component.errorString()));
    QVERIFY(QMetaObject::invokeMethod(rail.get(), "selectNext"));
    QCOMPARE(rail->property("selectedIndex").toInt(), 2);
    QVERIFY(QMetaObject::invokeMethod(rail.get(), "selectNext"));
    QCOMPARE(rail->property("selectedIndex").toInt(), 1);
    QVERIFY(QMetaObject::invokeMethod(rail.get(), "selectNext"));
    QCOMPARE(rail->property("selectedIndex").toInt(), 2);
    QVERIFY(QMetaObject::invokeMethod(rail.get(), "selectPrevious"));
    QCOMPARE(rail->property("selectedIndex").toInt(), 1);
}


void UiSmokeTests::exportModesAreSelectable()
{
    pfui::AppInfo::registerQmlTypes();
    QQmlApplicationEngine engine;
    auto* window = loadWindow(engine);
    QVERIFY(window);
    auto* popup = window->findChild<QObject*>("exportDialog");
    QVERIFY(popup);
    QVERIFY(QMetaObject::invokeMethod(popup, "open"));
    QTRY_VERIFY(popup->property("opened").toBool());
    const auto click = [&](const char* name) {
        auto* button = popup->findChild<QQuickItem*>(name);
        if (!button) return false;
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
            button->mapToScene(QPointF(button->width()/2, button->height()/2)).toPoint());
        return button->property("selected").toBool();
    };
    QCOMPARE(popup->property("selectedCutMode").toInt(), 0);
    QVERIFY(click("fastCutButton"));
    QCOMPARE(popup->property("selectedCutMode").toInt(), 1);
    QVERIFY(click("exactCutButton"));
    QCOMPARE(popup->property("selectedCutMode").toInt(), 0);
    QVERIFY(click("sortNumberingButton"));
    QCOMPARE(popup->property("selectedNumbering").toInt(), 1);
    QVERIFY(click("videoNumberingButton"));
    QCOMPARE(popup->property("selectedNumbering").toInt(), 0);
}

void UiSmokeTests::advancedOpensOnFirstClickAndStatusTranslates()
{
    pfui::AppInfo::registerQmlTypes();
    QQmlApplicationEngine engine;
    auto* window = loadWindow(engine);
    QVERIFY(window);
    auto* sources = window->findChild<QObject*>("sourcesRail");
    auto* section = sources->findChild<QQuickItem*>("advancedSection");
    QVERIFY(section);
    auto* button = section->findChild<QQuickItem*>("disclosureButton");
    QVERIFY(button);
    QQuickItem* flick = section->parentItem();
    while (flick && !flick->property("contentY").isValid()) flick = flick->parentItem();
    QVERIFY(flick);
    flick->setProperty("contentY", section->y() - 30);
    QTest::qWait(100);
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
        button->mapToScene(QPointF(button->width()/2, button->height()/2)).toPoint());
    QTRY_VERIFY(sources->property("advancedOpen").toBool());
    QVERIFY(section->property("expanded").toBool());
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nimport PfUi\nItem { function setLanguage(v) { L10n.language = v } }", QUrl());
    std::unique_ptr<QObject> control(component.create());
    QVERIFY(control);
    auto* label = window->findChild<QObject*>("gpuStatusLabel");
    QVERIFY(label);
    QVERIFY(QMetaObject::invokeMethod(control.get(), "setLanguage", Q_ARG(QVariant, "en")));
    QTRY_VERIFY(label->property("text").toString().contains("Ready"));
    QVERIFY(QMetaObject::invokeMethod(control.get(), "setLanguage", Q_ARG(QVariant, "ru")));
    QTRY_VERIFY(label->property("text").toString().contains(QString::fromUtf8("Готов")));
}

void UiSmokeTests::directMlDownloadIntegration()
{
    if (!qEnvironmentVariableIsSet("PF_TEST_DIRECTML_DOWNLOAD")) QSKIP("Opt-in network integration test");
    auto* info = pfui::AppInfo::instance();
    info->downloadProvider("dml");
    QTRY_VERIFY_WITH_TIMEOUT(!info->providerDownloading(), 300000);
    QCOMPARE(info->providerDownloadState(), QStringLiteral("installed"));
    const auto root = QString::fromStdString(pfservices::SettingsStore::defaultDirectory());
    QVERIFY(QFileInfo::exists(root + "/providers/dml/onnxruntime.dll"));
    QVERIFY(QFileInfo::exists(root + "/providers/dml/DirectML.dll"));
}

QTEST_MAIN(UiSmokeTests)
#include "test_ui_smoke.moc"
