#include <BackendProbeProcess.h>
#include <cstring>
// QTest smoke for the static QML module PfUi: Main.qml loads from resources,
// Theme/L10n singletons resolve, AppInfo bridge is registered.
#include <QtGui/QColor>
#include <QtGui/QGuiApplication>
#include <QtGui/QFontMetricsF>
#include <QtGui/QFontDatabase>
#include <QtCore/QDir>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlComponent>
#include <QtQml/QQmlProperty>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QQuickItem>
#include <QtTest/QTest>

#include <AppInfo.h>
#include <UiRuntime.h>
#include <ExportOrder.h>
#include <AnalysisController.h>
#include <QTemporaryDir>
#include <pfservices/SettingsStore.hpp>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QMediaPlayer>
#include <QVideoSink>
#include <QSignalSpy>
#include <functional>
#include <atomic>
#include <QElapsedTimer>
#include <QTimer>

class UiSmokeTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QCoreApplication::setOrganizationName("ParallelFinderTests");
        QCoreApplication::setOrganizationDomain("test.invalid");
        QCoreApplication::setApplicationName("UiAudit");
        if (qEnvironmentVariable("PF_UI_LANGUAGE") == "ru") {
            auto preferences=pfui::AppInfo::instance()->loadPreferences();
            preferences.insert("language","ru");
            pfui::AppInfo::instance()->savePreferences(preferences);
        }
        pfui::AppInfo::instance()->initializeBackendsAsync();
        QTRY_VERIFY_WITH_TIMEOUT(!pfui::AppInfo::instance()->backendInitializing(), 30000);
    }
    void mainQmlLoadsFromResources();
    void isolatedBackendProbeHandlesFailureTimeoutAndCancellation();
    void updateDialogDragsWithinWindow();
    void revealSettlesAndReducedMotionStops();
    void themeSingletonResolves();
    void appInfoBridgeResolves();
    void gpuInfoPropagatesToQml();
    void backendProbeDoesNotBlockUiAndPublishesReadiness();
    void backendProbeFailureLeavesHonestUnavailableState();
    void settingsAndNumericTypography();
    void repeatSearchTogglesFromTheActualSidebar();
    void appearancePersistsAndRejectsMissingFonts();
    void slowSettingsWritesDoNotBlockUiAndKeepOtherSections();
    void appearanceAndVideoInspectionKeepUiResponsive();
    void resultNavigationStopsAtEnds();
    void sourcesLiveInsideDropAreaAboveActions();
    void sourceWheelDoesNotMoveSettingsRail();
    void pairColorsAndExportOrder();
    void exportColorOrderControlsFileNumbering();
    void dialogFramesAdvanceDuringRealEventLoop();
    void modalCloseKeepsNativeBordersAndFades();
    void appearanceRefreshesBackdropBeforeClosing();
    void settingsDragAndScrollKeepRendering();
    void finiteAnimationsRespectReducedMotion();
    void idleWorkspaceStopsRequestingFrames();
    void resultArrowKeysWorkAfterSourceButtonFocus();
    void selectsAllResultsWithoutDisplayLimit();
    void exportModesAreSelectable();
    void advancedOpensOnFirstClickAndStatusTranslates();
    void directMlDownloadIntegration();
    void cacheFolderAcceptsLocalFileUrls();
    void staticResultsShowPlaybackControls();
    void previewIsEmbeddedAndStopsOnRecordChange();
    void inlinePairActuallyDecodesAndStopsAtClipEnd();
    void matcherStagesTranslateWithActualCounters();
    void resultLabelsFollowMatchTypeAndLanguage();
    void sourceStatisticsFollowSelectionAndInspectionScope();
};

void UiSmokeTests::isolatedBackendProbeHandlesFailureTimeoutAndCancellation()
{
    const auto executable = QCoreApplication::applicationFilePath();
    const auto result = pfui::probeBackendProcess(executable, {}, {"--pf-test-backend-child","success"});
    QVERIFY(result.ortLoaded); QCOMPARE(result.ortVersion,std::string("test-runtime"));
    QCOMPARE(result.backends.size(),std::size_t(1)); QVERIFY(result.backends[0].available);
    QVERIFY_EXCEPTION_THROWN(pfui::probeBackendProcess(executable,{}, {"--pf-test-backend-child","invalid"}),std::runtime_error);
    QVERIFY_EXCEPTION_THROWN(pfui::probeBackendProcess(executable,{}, {"--pf-test-backend-child","failure"}),std::runtime_error);
    QElapsedTimer elapsed; elapsed.start();
    QVERIFY_EXCEPTION_THROWN(pfui::probeBackendProcess(executable,{}, {"--pf-test-backend-child","slow"},100),std::runtime_error);
    QVERIFY(elapsed.elapsed()<3000);
    std::stop_source stop;
    std::jthread cancel([&] { std::this_thread::sleep_for(std::chrono::milliseconds(100)); stop.request_stop(); });
    elapsed.restart();
    QVERIFY_EXCEPTION_THROWN(pfui::probeBackendProcess(executable,stop.get_token(), {"--pf-test-backend-child","slow"}),std::runtime_error);
    QVERIFY(elapsed.elapsed()<3000);
    pfgpu::BackendProbe expected; expected.ortLoaded=true; expected.backends={{pfgpu::Provider::Cpu,true,"","CPU"}};
    const auto json=pfui::backendProbeJson(expected);
    QVERIFY(pfui::backendProbeFromJson(json).ortLoaded);
    QVERIFY_EXCEPTION_THROWN(pfui::backendProbeFromJson("{}"),std::runtime_error);
}

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
    auto* modelCombo = window->findChild<QObject*>("poseModelComboBox");
    QVERIFY(modelCombo);
    const auto files = window->findChild<QObject*>("sourcesRail")->property("modelFiles").toList();
    int expectedIndex = files.indexOf(pfui::AnalysisController::instance()->modelChoice());
    if (expectedIndex < 0) expectedIndex = files.indexOf(QStringLiteral("yolo26m-pose.onnx"));
    QTRY_COMPARE(modelCombo->property("currentIndex").toInt(), expectedIndex);
}

void UiSmokeTests::updateDialogDragsWithinWindow()
{
    pfui::AppInfo::registerQmlTypes();
    QQmlApplicationEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
import QtQuick
import PfUi
Window {
    id: testWindow
    width: 1000; height: 700; visible: true
    QtObject {
        id: fake
        property string state: "available"
        property string currentVersion: "0.1.0-rc.17.1"
        property string newVersion: "0.1.0-rc.18"
        property string changelog: "Changes"
        property string error: ""
        property bool dialogVisible: true
        property bool busy: false
        property int totalBytes: 1024
        property int receivedBytes: 0
        property real bytesPerSecond: 0
        property real progress: 0
        signal changed()
        function later() { dialogVisible = false; changed() }
    }
    UpdateDialog { rootWindow: testWindow; service: fake }
}
)", QUrl());
    QScopedPointer<QObject> object(component.create());
    QVERIFY2(object, qPrintable(component.errorString()));
    auto* window = qobject_cast<QQuickWindow*>(object.data());
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto* popup = window->findChild<QObject*>("updateDialog");
    auto* handle = window->findChild<QQuickItem*>("updateDragArea");
    QVERIFY(popup && handle);
    QTRY_VERIFY(popup->property("opened").toBool());
    const double x = popup->property("x").toDouble(), y = popup->property("y").toDouble();
    const auto start = handle->mapToScene(QPointF(60, 20)).toPoint();
    QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(window, start + QPoint(90, 55), 20);
    QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, start + QPoint(90, 55));
    QTRY_VERIFY(qAbs(popup->property("x").toDouble() - x - 90) < 2);
    QTRY_VERIFY(qAbs(popup->property("y").toDouble() - y - 55) < 2);
    popup->setProperty("positionOffsetX", 10000);
    popup->setProperty("positionOffsetY", -10000);
    QTRY_VERIFY(popup->property("x").toDouble() + popup->property("width").toDouble() <= window->width() - 11);
    QCOMPARE(popup->property("y").toDouble(), 12.0);
    window->resize(720, 500);
    QTRY_VERIFY(popup->property("x").toDouble() + popup->property("width").toDouble() <= window->width() - 11);
}

void UiSmokeTests::revealSettlesAndReducedMotionStops()
{
    pfui::AppInfo::registerQmlTypes();QQmlApplicationEngine engine;QQmlComponent component(&engine);
    component.setData(R"(
import QtQuick
import PfUi
Window {
    width: 200; height: 80; visible: true
PfReveal {
    objectName: "revealTest"
    anchors.fill: parent; delay: 35
    property bool originalMotion: Theme.reducedMotion
    function reduce(value) { Theme.reducedMotion = value }
    Rectangle { anchors.fill: parent; color: Theme.panel }
}
}
)",QUrl());
    QScopedPointer<QObject> testWindow(component.create());QVERIFY2(testWindow,qPrintable(component.errorString()));
    auto* window=qobject_cast<QQuickWindow*>(testWindow.data());QVERIFY(window);
    QVERIFY(QTest::qWaitForWindowExposed(window));
    auto* item=testWindow->findChild<QObject*>("revealTest"); QVERIFY(item);
    const auto original=item->property("originalMotion").toBool();
    QVERIFY(QMetaObject::invokeMethod(item,"reduce",Q_ARG(QVariant,false)));
    item->setProperty("active",true);QCOMPARE(item->property("reveal").toDouble(),0.0);
    // Native rendering starts after exposure; wait for the actual final frame
    // rather than sampling a nearly-finished easing curve at a fixed instant.
    QTRY_COMPARE_WITH_TIMEOUT(item->property("reveal").toDouble(),1.0,1500);
    QEventLoop loop;
    QTimer::singleShot(80,&loop,&QEventLoop::quit); loop.exec(); QCOMPARE(item->property("reveal").toDouble(),1.0);
    item->setProperty("active",false);item->setProperty("active",true);
    QVERIFY(QMetaObject::invokeMethod(item,"reduce",Q_ARG(QVariant,true)));QCOMPARE(item->property("reveal").toDouble(),1.0);
    item->setProperty("active",false);
    QVERIFY(QMetaObject::invokeMethod(item,"reduce",Q_ARG(QVariant,original)));
}

namespace {
QQuickWindow* loadWindow(QQmlApplicationEngine& engine)
{
    // See app/main.cpp: explicit import path + URL load for static-module
    // resources under shared Qt.
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/PfUi/qml/Main.qml")));
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().value(0, nullptr));
    if (window && !QTest::qWaitForWindowExposed(window)) return nullptr;
    return window;
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

void UiSmokeTests::backendProbeDoesNotBlockUiAndPublishesReadiness()
{
    pfui::AppInfo info;
    QElapsedTimer getterTime; getterTime.start();
    QVERIFY(!info.backendAvailable("cuda"));
    QVERIFY(!info.backendReason("cuda").isEmpty());
    QVERIFY(getterTime.elapsed() < 100);
    std::atomic_bool release{false};
    QSignalSpy ready(&info, &pfui::AppInfo::backendInitializationChanged);
    QElapsedTimer elapsed;
    elapsed.start();
    info.prepareBackendInitialization();
    info.initializeBackendsAsync([&] {
        while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        pfgpu::BackendProbe result;
        result.ortLoaded = true;
        result.ortVersion = "test";
        result.backends.push_back({pfgpu::Provider::Cuda, true, {}, "test GPU"});
        result.backends.push_back({pfgpu::Provider::Cpu, true, {}, "test CPU"});
        return result;
    });
    const bool pending = info.backendInitializing();
    const bool notReady = !info.backendAvailable("cuda");
    bool eventDelivered = false;
    QTimer::singleShot(0, &info, [&] { eventDelivered = true; });
    QTest::qWait(20);
    const auto responsiveMs = elapsed.elapsed();
    release.store(true); // release before any assertion can unwind/join
    QTRY_VERIFY(!info.backendInitializing());
    QVERIFY(pending);
    QVERIFY(notReady);
    QVERIFY(eventDelivered);
    QVERIFY(responsiveMs < 200);
    QVERIFY(info.backendAvailable("cuda"));
    QVERIFY(info.backendAvailable("auto"));
    QCOMPARE(info.gpuDevice(), QStringLiteral("test GPU"));
    QCOMPARE(ready.count(), 2);
}

void UiSmokeTests::backendProbeFailureLeavesHonestUnavailableState()
{
    pfui::AppInfo info;
    info.initializeBackendsAsync([]() -> pfgpu::BackendProbe { throw std::runtime_error("test probe failure"); });
    QTRY_VERIFY(!info.backendInitializing());
    QVERIFY(!info.backendAvailable("auto"));
    QVERIFY(!info.backendAvailable("cuda"));
    QCOMPARE(info.backendReason("auto"), QStringLiteral("test probe failure"));
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

void UiSmokeTests::repeatSearchTogglesFromTheActualSidebar()
{
    pfui::AppInfo::registerQmlTypes();
    auto* analysis=pfui::AnalysisController::instance();
    const bool original=analysis->expandedSearch();
    analysis->setExpandedSearch(false);
    QQmlApplicationEngine engine;
    auto* window=loadWindow(engine);
    QVERIFY(window);
    auto* sources=window->findChild<QObject*>("sourcesRail");
    QVERIFY(sources);
    auto* repeat=sources->findChild<QQuickItem*>("expandedSearchCheck");
    auto* options=sources->findChild<QQuickItem*>("sourceOptionsFlick");
    QVERIFY(repeat && options);
    QVERIFY(repeat->isEnabled());
    options->setProperty("contentY",std::max(0.0,
        repeat->mapToItem(options,QPointF()).y()-options->height()+repeat->height()+18));
    QTest::qWait(100);
    const auto position=repeat->mapToScene(QPointF(repeat->width()/2,repeat->height()/2)).toPoint();
    QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,position);
    QTRY_VERIFY(analysis->expandedSearch());
    QTRY_VERIFY(repeat->property("checked").toBool());
    QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,position);
    QTRY_VERIFY(!analysis->expandedSearch());
    QTRY_VERIFY(!repeat->property("checked").toBool());
    analysis->setExpandedSearch(original);
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
    QVERIFY(!sources->findChild<QObject*>("costumeModeCheck"));
    QVERIFY(!popup->findChild<QObject*>("costumeModeCheck"));
    auto* repeat = sources->findChild<QQuickItem*>("expandedSearchCheck");
    auto* options = sources->findChild<QQuickItem*>("sourceOptionsFlick");
    QVERIFY(repeat && options);
    QVERIFY(!repeat->property("text").toString().isEmpty());
    if (!capture.isEmpty()) {
        options->setProperty("contentY", std::max(0.0,
            repeat->mapToItem(options, QPointF()).y() - options->height() + repeat->height() + 18));
        QTest::qWait(100);
        QVERIFY(window->grabWindow().save(capture + "/repeat-search.png"));
        options->setProperty("contentY", 0);
    }
    QVERIFY(QMetaObject::invokeMethod(popup, "open"));
    if (!capture.isEmpty()) {
        for (int frame = 0; frame < 18; ++frame) {
            QTest::qWait(25);
            QVERIFY(window->grabWindow().save(capture + QString("/dialog-entrance-%1.png").arg(frame, 3, 10, QChar('0'))));
        }
    }
    QTRY_VERIFY(popup->property("opened").toBool());
    if (!capture.isEmpty()) { QTest::qWait(300); QVERIFY(window->grabWindow().save(capture + "/settings-analysis.png")); }
    auto* tabs = popup->findChild<QObject*>("settingsTabs");
    QVERIFY(tabs); tabs->setProperty("currentIndex", 1);
    auto* tab = popup->findChild<QQuickItem*>("settingsAppearanceTab");
    auto* indicator = popup->findChild<QQuickItem*>("settingsTabIndicator");
    QVERIFY(tab && indicator);
    QCOMPARE(tab->height(), indicator->height());
    QVERIFY(indicator->height() >= 32);
    if (!capture.isEmpty()) {
        for (int frame=0;frame<14;++frame) {
            QTest::qWait(25);
            QVERIFY(window->grabWindow().save(capture+QString("/settings-motion-%1.png").arg(frame,3,10,QChar('0'))));
        }
    }
    QTest::qWait(100);
    if (!capture.isEmpty()) QVERIFY(window->grabWindow().save(capture + "/settings-appearance.png"));
    QVERIFY(QMetaObject::invokeMethod(popup, "close"));
    QTRY_VERIFY(!popup->property("visible").toBool());
    QVERIFY2(warnings.isEmpty(), qPrintable(warnings.join('\n')));
}

void UiSmokeTests::appearancePersistsAndRejectsMissingFonts()
{
    pfui::AppInfo::registerQmlTypes();
    const auto saved = pfui::AppInfo::instance()->loadPreferences();
    struct RestorePreferences {
        QVariantMap value;
        ~RestorePreferences() { pfui::AppInfo::instance()->savePreferences(value); }
    } restore{saved};
    for (int pass = 0; pass < 2; ++pass) {
        QQmlApplicationEngine engine;
        auto* window = loadWindow(engine);
        QVERIFY(window);
        auto* popup = window->findChild<QObject*>("settingsDialog");
        QVERIFY(popup);
        auto* accentChoice = popup->findChild<QObject*>("appearanceAccentChoice");
        QVERIFY(accentChoice);
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nimport PfUi\nItem { property color accent: Theme.accent; property string family: Theme.fontFamily; property bool reduced: Theme.reducedMotion; function reduce() { Theme.reducedMotion = true } }", QUrl());
        std::unique_ptr<QObject> probe(component.create());
        QVERIFY2(probe, qPrintable(component.errorString()));
        if (pass == 0) {
            for (const auto& color : {"blue", "orange", "blue"}) {
                QVERIFY(QMetaObject::invokeMethod(popup, "applyAccent", Q_ARG(QVariant, QString::fromLatin1(color))));
                QCOMPARE(probe->property("accent").value<QColor>().name(), QString::fromLatin1(color == std::string_view("blue") ? "#5d8dde" : "#d97757"));
                QCOMPARE(accentChoice->property("currentIndex").toInt(), color == std::string_view("blue") ? 1 : 0);
            }
            const auto family = probe->property("family").toString();
            QVERIFY(QFontDatabase::families().contains(family));
            QVariant accepted;
            QVERIFY(QMetaObject::invokeMethod(popup, "applyFont", Q_RETURN_ARG(QVariant, accepted), Q_ARG(QVariant, QStringLiteral("PF nonexistent font 92731"))));
            QVERIFY(!accepted.toBool());
            QCOMPARE(probe->property("family").toString(), family);
            QVERIFY(!popup->property("themeStatus").toString().isEmpty());
        } else {
            QCOMPARE(probe->property("accent").value<QColor>().name(), QStringLiteral("#5d8dde"));
            QVERIFY(QMetaObject::invokeMethod(popup, "open"));
            QTRY_VERIFY(popup->property("opened").toBool());
            QCOMPARE(accentChoice->property("currentIndex").toInt(), 1);
            QVERIFY(QMetaObject::invokeMethod(popup, "close"));
            QVERIFY(QMetaObject::invokeMethod(probe.get(), "reduce"));
            QVERIFY(probe->property("reduced").toBool());
            QVERIFY(QMetaObject::invokeMethod(popup, "resetAppearance"));
            QVERIFY(!probe->property("reduced").toBool());
            QCOMPARE(probe->property("accent").value<QColor>().name(), QStringLiteral("#d97757"));
            const auto preferences = pfui::AppInfo::instance()->loadPreferences();
            QVERIFY(!preferences.value("reducedMotion").toBool());
            QVERIFY(preferences.value("customFontPath").toString().isEmpty());
        }
    }
}

void UiSmokeTests::idleWorkspaceStopsRequestingFrames()
{
    pfui::AppInfo::registerQmlTypes();
    QQmlApplicationEngine engine;
    auto* window = loadWindow(engine);
    QVERIFY(window);
    QEventLoop idleLoop;
    QTimer::singleShot(1500,&idleLoop,&QEventLoop::quit); idleLoop.exec(); // settle through the normal event loop
    QSignalSpy frames(window, &QQuickWindow::frameSwapped);
    QVERIFY(frames.isValid());
    QTimer::singleShot(350,&idleLoop,&QEventLoop::quit); idleLoop.exec();
    QVERIFY2(frames.count() <= 3, qPrintable(QString("Idle workspace rendered %1 frames").arg(frames.count())));
}

void UiSmokeTests::finiteAnimationsRespectReducedMotion()
{
    pfui::AppInfo::registerQmlTypes();
    QQmlApplicationEngine engine;
    QQmlComponent component(&engine);
    component.setData(R"(
import QtQuick
import PfUi
Window {
    width: 800; height: 600; visible: true
    function reduce(value) { Theme.reducedMotion = value }
    function swap(id) { panel.record = {id: id, matchType: 'pose', leftStart: id, leftEnd: id + 1} }
    ComparisonView { id: panel; objectName: "animationPanel"; width: 300; height: 400 }
    CollapsibleSection { objectName: "animationDisclosure"; x: 320; width: 200; Rectangle { width: 200; height: 100 } }
    ResultsRail { objectName: "animationResults"; x: 530; width: 270; height: 600; results: [{id: 1, similarity: .8}, {id: 2, similarity: .9}] }
})", QUrl());
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root != nullptr, qPrintable(component.errorString()));
    auto* panel = root->findChild<QObject*>("animationPanel");
    auto* disclosure = root->findChild<QObject*>("animationDisclosure");
    QVERIFY(panel && disclosure);
    auto* animationWindow = qobject_cast<QQuickWindow*>(root.get());
    QVERIFY(animationWindow);
    QTRY_VERIFY_WITH_TIMEOUT(animationWindow->isExposed(), 5000);
    QSignalSpy animationFrames(animationWindow, &QQuickWindow::frameSwapped);
    QVERIFY(QMetaObject::invokeMethod(root.get(), "reduce", Q_ARG(QVariant, false)));
    QVERIFY(QMetaObject::invokeMethod(root.get(), "swap", Q_ARG(QVariant, 1)));
    QVERIFY(!panel->findChild<QObject*>("inlineMediaPlayer"));
    QVERIFY(panel->property("transitionRunning").toBool());
    QEventLoop animationLoop;
    QTimer::singleShot(650, &animationLoop, &QEventLoop::quit); animationLoop.exec();
    qInfo("Pair transition after 650 ms: frames=%lld phase=%g running=%d",
        static_cast<long long>(animationFrames.count()), panel->property("pairSignalPhase").toDouble(),
        panel->property("transitionRunning").toBool());
    QVERIFY(!panel->property("transitionRunning").toBool());
    QCOMPARE(panel->property("pairSignalPhase").toDouble(), 0.0);
    disclosure->setProperty("expanded", true);
    QTimer::singleShot(300, &animationLoop, &QEventLoop::quit); animationLoop.exec();
    QCOMPARE(disclosure->property("reveal").toDouble(), 1.0);
    QVERIFY(QMetaObject::invokeMethod(root.get(), "swap", Q_ARG(QVariant, 2)));
    QVERIFY(panel->property("transitionRunning").toBool());
    QVERIFY(QMetaObject::invokeMethod(root.get(), "reduce", Q_ARG(QVariant, true)));
    QVERIFY(!panel->property("transitionRunning").toBool());
    QCOMPARE(panel->property("pairSignalPhase").toDouble(), 0.0);
    QVERIFY(QMetaObject::invokeMethod(root.get(), "swap", Q_ARG(QVariant, 3)));
    QVERIFY(!panel->property("transitionRunning").toBool());
    disclosure->setProperty("expanded", false);
    QCOMPARE(disclosure->property("reveal").toDouble(), 0.0);
    QVERIFY(QMetaObject::invokeMethod(root.get(), "reduce", Q_ARG(QVariant, false)));
    disclosure->setProperty("expanded", true);
    QTest::qWait(30);
    QVERIFY(QMetaObject::invokeMethod(root.get(), "reduce", Q_ARG(QVariant, true)));
    QCOMPARE(disclosure->property("reveal").toDouble(), 1.0);
    QVERIFY(QMetaObject::invokeMethod(root.get(), "reduce", Q_ARG(QVariant, false)));
}

void UiSmokeTests::sourcesLiveInsideDropAreaAboveActions()
{
    pfui::AppInfo::registerQmlTypes();
    QQmlApplicationEngine engine;
    auto* window = loadWindow(engine);
    QVERIFY(window);
    auto* sources = window->findChild<QObject*>("sourcesRail");
    QVERIFY(sources);
    sources->setProperty("sourceFiles", QStringList{"D:/one.mp4", "D:/two.mp4"});
    auto* list = sources->findChild<QQuickItem*>("loadedSourcesList");
    auto* drop = sources->findChild<QQuickItem*>("sourceDropArea");
    auto* remove = sources->findChild<QQuickItem*>("removeSourceButton");
    auto* clear = sources->findChild<QQuickItem*>("clearSourcesButton");
    QVERIFY(list && drop && remove && clear);
    QCOMPARE(list->parentItem(), drop);
    QTest::qWait(50);
    QVERIFY(list->mapToScene(QPointF(0, list->height())).y() < clear->mapToScene(QPointF()).y());
    QVERIFY(!remove->isEnabled());
    const auto capture = qEnvironmentVariable("PF_UI_CAPTURE_DIR");
    if (!capture.isEmpty()) { QDir().mkpath(capture); QVERIFY(window->grabWindow().save(capture + "/loaded-sources.png")); }
    sources->setProperty("selectedSourceIndex", 1);
    QVERIFY(remove->isEnabled());
    sources->setProperty("sourceFiles", QStringList{});
    QVERIFY(!remove->isEnabled()); QVERIFY(!clear->isEnabled());
}

void UiSmokeTests::sourceWheelDoesNotMoveSettingsRail()
{
    pfui::AppInfo::registerQmlTypes();QQmlApplicationEngine engine;
    auto* window=loadWindow(engine);QVERIFY(window);
    auto* sources=window->findChild<QObject*>("sourcesRail");
    QStringList files;for(int i=0;i<30;++i)files<<QString("D:/source-%1.mp4").arg(i);
    sources->setProperty("sourceFiles",files);QTest::qWait(80);
    auto* outer=window->findChild<QQuickItem*>("sourceOptionsFlick");
    auto* list=window->findChild<QQuickItem*>("loadedSourcesList");QVERIFY(outer && list);
    const auto point=list->mapToScene(QPointF(list->width()/2,list->height()/2));
    const auto initial=outer->property("contentY").toDouble();
    QWheelEvent event(point,window->mapToGlobal(point.toPoint()),QPoint(),QPoint(0,-120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
    QCoreApplication::sendEvent(window,&event);QTest::qWait(50);
    QCOMPARE(outer->property("contentY").toDouble(),initial);
    QVERIFY(list->property("contentY").toDouble()>0);
    list->setProperty("contentY",list->property("contentHeight").toDouble()-list->height());
    QWheelEvent endEvent(point,window->mapToGlobal(point.toPoint()),QPoint(),QPoint(0,-120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
    QCoreApplication::sendEvent(window,&endEvent);QTest::qWait(50);
    QCOMPARE(outer->property("contentY").toDouble(),initial);
}

void UiSmokeTests::pairColorsAndExportOrder()
{
    pfui::AppInfo::registerQmlTypes();
    QQmlApplicationEngine engine;
    auto* window = loadWindow(engine); QVERIFY(window);
    auto* rail = window->findChild<QObject*>("resultsRail"); QVERIFY(rail);
    const QVariantList records{
        QVariantMap{{"id",0},{"similarity",.9},{"categoryColor","#D56565"}},
        QVariantMap{{"id",1},{"similarity",.8},{"categoryColor","#638EDB"}},
        QVariantMap{{"id",2},{"similarity",.7}}};
    rail->setProperty("results", records);
    rail->setProperty("selectedRows", QVariantMap{{"0",true},{"1",true},{"2",true}});
    QTest::qWait(100);
    QVERIFY(!rail->findChild<QObject*>("classificationFilterCombo"));
    QVERIFY(!rail->findChild<QObject*>("categoryFilterCombo"));
    QVERIFY(!rail->findChild<QObject*>("colorFilterCombo"));
    QVERIFY(!rail->findChild<QObject*>("resultCategoryEditor"));
    QCOMPARE(rail->property("selectedCount").toInt(),3);
    std::function<QQuickItem*(QQuickItem*, const QString&)> findVisual = [&](QQuickItem* item, const QString& name) -> QQuickItem* {
        if (item->objectName() == name) return item;
        for (auto* child : item->childItems()) if (auto* result = findVisual(child, name)) return result;
        return nullptr;
    };
    QVERIFY(!window->grabWindow().isNull());
    auto* color = findVisual(window->contentItem(),"resultColorButton0"); QVERIFY(color);
    QVERIFY(!window->grabWindow().isNull());
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, color->mapToScene(QPointF(9,14)).toPoint());
    auto* picker = rail->findChild<QObject*>("resultColorPicker"); QVERIFY(picker);
    QTRY_VERIFY(picker->property("opened").toBool());
    QCOMPARE(rail->property("colorTargetId").toInt(),0);
    QVERIFY(!window->grabWindow().isNull());
    QVERIFY(findVisual(window->contentItem(),"resultPaletteColor0"));
    QVERIFY(findVisual(window->contentItem(),"resultPaletteColor5"));
    QSignalSpy requested(rail, SIGNAL(pairColorRequested(int,QString,QString))); QVERIFY(requested.isValid());
    auto* blue = findVisual(window->contentItem(),"resultPaletteColor2"); QVERIFY(blue);
    QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,blue->mapToScene(QPointF(14,14)).toPoint());
    QCOMPARE(requested.size(),1);
    QCOMPARE(requested[0][0].toInt(),0);
    QCOMPARE(requested[0][2].toString(),QString("#638EDB"));
    QCOMPARE(rail->property("selectedCount").toInt(),3);
    QTRY_VERIFY(!picker->property("visible").toBool());
    QTest::mouseMove(window,QPoint(650,400));
    auto* popup = window->findChild<QObject*>("exportDialog"); QVERIFY(popup);
    popup->setProperty("results", records);
    popup->setProperty("selectedRows", QVariantMap{{"0",true},{"1",true},{"2",true}});
    QCOMPARE(popup->property("colorOrder").value<QJSValue>().toVariant().toStringList(), (QStringList{"#d56565","#638edb",""}));
    popup->setProperty("selectionOrder", QVariantList{2,0,1});
    QVariant selected;
    QVERIFY(QMetaObject::invokeMethod(popup,"selectedIndexes",Q_RETURN_ARG(QVariant,selected)));
    QCOMPARE(selected.value<QJSValue>().toVariant().toList(), (QVariantList{2,0,1}));
    QVERIFY(QMetaObject::invokeMethod(popup,"moveColor", Q_ARG(QVariant,1),Q_ARG(QVariant,-1)));
    QCOMPARE(popup->property("colorOrder").value<QJSValue>().toVariant().toStringList(), (QStringList{"#638edb","#d56565",""}));
    QVERIFY(QMetaObject::invokeMethod(popup,"moveColor", Q_ARG(QVariant,0),Q_ARG(QVariant,-1)));
    QCOMPARE(popup->property("colorOrder").value<QJSValue>().toVariant().toStringList().first(),QString("#638edb"));
    const auto capture=qEnvironmentVariable("PF_UI_CAPTURE_DIR");
    if(!capture.isEmpty()) {
        QDir().mkpath(capture); QVERIFY(window->grabWindow().save(capture+"/pair-colors.png"));
        QVERIFY(QMetaObject::invokeMethod(popup,"open"));
        QEventLoop loop; QTimer::singleShot(550,&loop,&QEventLoop::quit); loop.exec();
        QVERIFY(window->grabWindow().save(capture+"/export-color-order.png"));
    }
}

void UiSmokeTests::exportColorOrderControlsFileNumbering()
{
    std::vector<pfcore::MotionMatch> matches(5);
    for (int i=0;i<5;++i) { matches[i].leftSourceId="one.mp4"; matches[i].leftStartSeconds=50-i*10; }
    const QVariantList records{
        QVariantMap{{"categoryColor","#D56565"}}, QVariantMap{{"categoryColor","#638EDB"}},
        QVariantMap{{"categoryColor","#d56565"}}, QVariantMap{}, QVariantMap{{"categoryColor","#abcdef"}}};
    const QVariantList indexes{0,1,2,3,4,1,-1,50,"invalid"};
    const QStringList colors{"#638edb","#d56565",""};
    QCOMPARE(pfui::orderedExportIndexes(indexes,matches,records,0,colors), (std::vector<int>{1,2,0,3,4}));
    QCOMPARE(pfui::orderedExportIndexes(indexes,matches,records,1,colors), (std::vector<int>{1,0,2,3,4}));
    QCOMPARE(pfui::orderedExportIndexes(indexes,matches,records,0,{}), (std::vector<int>{4,3,2,1,0}));
    QCOMPARE(pfui::orderedExportIndexes(indexes,matches,records,1,{}), (std::vector<int>{0,1,2,3,4}));
}

void UiSmokeTests::settingsDragAndScrollKeepRendering()
{
    pfui::AppInfo::registerQmlTypes(); QQmlApplicationEngine engine;
    auto* window=loadWindow(engine); QVERIFY(window);
    auto* popup=window->findChild<QObject*>("settingsDialog"); QVERIFY(popup);
    auto* handle=window->findChild<QQuickItem*>("settingsDragArea"); QVERIFY(handle);
    auto* flick=window->findChild<QQuickItem*>("settingsAnalysisFlick"); QVERIFY(flick);
    window->findChild<QObject*>("settingsTabs")->setProperty("currentIndex",0);
    QEventLoop loop;
    const auto wait=[&](int ms) { QTimer::singleShot(ms,&loop,&QEventLoop::quit); loop.exec(); };
    QMetaObject::invokeMethod(popup,"open"); wait(1500);
    QElapsedTimer elapsed; elapsed.start();
    qint64 last=-1,maxGap=0; int frames=0;
    const auto connection=connect(window,&QQuickWindow::frameSwapped,&loop,[&] {
        const auto now=elapsed.elapsed(); if(last>=0) maxGap=std::max(maxGap,now-last);
        last=now; ++frames;
    },Qt::QueuedConnection);
    const auto start=handle->mapToScene(QPointF(80,20)).toPoint();
    const auto originalX=popup->property("x").toDouble();
    QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,start);
    int step=0;
    QTimer input; input.setTimerType(Qt::PreciseTimer); input.setInterval(8);
    connect(&input,&QTimer::timeout,&loop,[&] { ++step; QTest::mouseMove(window,start+QPoint(step,step/3),0); });
    input.start(); wait(600); input.stop();
    QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,start+QPoint(step,step/3));
    wait(50);
    QVERIFY(popup->property("x").toDouble()>originalX+20);
    const auto dragFrames=frames; const auto dragGap=maxGap;
    frames=0;last=-1;maxGap=0;
    const auto point=flick->mapToScene(QPointF(5,300));
    disconnect(&input,nullptr,&loop,nullptr); input.setInterval(24);
    connect(&input,&QTimer::timeout,&loop,[&] {
        QWheelEvent event(point,window->mapToGlobal(point.toPoint()),QPoint(),QPoint(0,-120),Qt::NoButton,Qt::NoModifier,Qt::NoScrollPhase,false);
        QCoreApplication::sendEvent(window,&event);
    });
    input.start(); wait(600); input.stop(); disconnect(connection);
    qInfo("Settings input: drag=%d frames max_gap=%lld ms; scroll=%d frames max_gap=%lld ms",dragFrames,static_cast<long long>(dragGap),frames,static_cast<long long>(maxGap));
    QVERIFY(flick->property("contentY").toDouble()>20);
    QVERIFY(dragFrames>=20); QVERIFY(frames>=15);
    QVERIFY(dragGap<100); QVERIFY(maxGap<100);
}

void UiSmokeTests::appearanceRefreshesBackdropBeforeClosing()
{
    pfui::AppInfo::registerQmlTypes(); QQmlApplicationEngine engine;
    auto* window=loadWindow(engine); QVERIFY(window);
    auto* popup=window->findChild<QObject*>("settingsDialog"); QVERIFY(popup);
    auto* button=window->findChild<QQuickItem*>("sourcePrimaryAction"); QVERIFY(button);
    auto* refresh=window->findChild<QObject*>("modalBackdropRefresh"); QVERIFY(refresh);
    QEventLoop loop;
    const auto wait=[&](int milliseconds) { QTimer::singleShot(milliseconds,&loop,&QEventLoop::quit); loop.exec(); };
    wait(1500);
    QVERIFY(QMetaObject::invokeMethod(popup,"applyAccent",Q_ARG(QVariant,"orange")));
    QVERIFY(QMetaObject::invokeMethod(popup,"open")); wait(650);
    const auto sample=[&] {
        const auto image=window->grabWindow();
        const auto point=button->mapToScene(QPointF(25,button->height()/2))*window->devicePixelRatio();
        return image.pixelColor(point.toPoint());
    };
    const auto before=sample();
    QVERIFY(before.red()>before.blue());
    QVERIFY(QMetaObject::invokeMethod(popup,"applyAccent",Q_ARG(QVariant,"blue")));
    wait(250);
    QVERIFY(popup->property("visible").toBool());
    QVERIFY(!refresh->property("running").toBool());
    const auto after=sample();
    qInfo()<<"Backdrop color while settings remain open:"<<before<<after;
    QVERIFY2(after.blue()>after.red(),"Background must show the newly selected blue accent before closing settings");
    QVERIFY(after!=before);
    wait(150); // drain the explicit screenshot readback before measuring idle
    QSignalSpy frames(window,&QQuickWindow::frameSwapped); wait(350);
    QVERIFY2(frames.count()<=3,qPrintable(QString("Appearance refresh must stop: %1 idle frames").arg(frames.count())));
    const auto capture=qEnvironmentVariable("PF_UI_CAPTURE_DIR");
    if(!capture.isEmpty()) { QDir().mkpath(capture); window->grabWindow().save(capture+"/live-accent-blue.png"); }
    QVERIFY(QMetaObject::invokeMethod(popup,"applyAccent",Q_ARG(QVariant,"orange")));
    wait(250);
}

void UiSmokeTests::modalCloseKeepsNativeBordersAndFades()
{
    pfui::AppInfo::registerQmlTypes(); QQmlApplicationEngine engine;
    auto* window=loadWindow(engine); QVERIFY(window);
    auto* popup=window->findChild<QObject*>("settingsDialog"); QVERIFY(popup);
    auto* backdrop=window->findChild<QQuickItem*>("modalBackdrop"); QVERIFY(backdrop);
    auto* workspace=window->findChild<QQuickItem*>("workspaceSurface"); QVERIFY(workspace);
    QEventLoop loop;
    bool fading=false, settled=false, nativeRaster=true;
    int captured=0;
    const auto capture=qEnvironmentVariable("PF_UI_CAPTURE_DIR");
    if(!capture.isEmpty()) QDir().mkpath(capture);
    QTimer::singleShot(100,popup,[popup]{QMetaObject::invokeMethod(popup,"open");});
    QTimer::singleShot(650,&loop,[&]{
        nativeRaster &= !QQmlProperty(workspace,"layer.enabled").read().toBool();
        if(!capture.isEmpty()) window->grabWindow().save(capture+"/modal-open.png");
        QMetaObject::invokeMethod(popup,"close");
        QTimer::singleShot(30,&loop,[&]{
            fading=!backdrop->property("active").toBool() && backdrop->opacity()>0 && backdrop->opacity()<1;
            qInfo("Close fade: opacity=%f active=%d",backdrop->opacity(),backdrop->property("active").toBool());
        });
    });
    QTimer captures;
    captures.setTimerType(Qt::PreciseTimer); captures.setInterval(16);
    connect(&captures,&QTimer::timeout,&loop,[&]{
        nativeRaster &= !QQmlProperty(workspace,"layer.enabled").read().toBool();
        if(!capture.isEmpty()) window->grabWindow().save(capture+QString("/modal-close-%1.png").arg(captured,3,10,QChar('0')));
        ++captured;
    });
    QTimer::singleShot(655,&loop,[&]{captures.start();});
    QTimer::singleShot(1050,&loop,[&]{ captures.stop(); });
    QTimer::singleShot(1350,&loop,[&]{
        settled=!popup->property("visible").toBool() && backdrop->opacity()<.001;
        if(!capture.isEmpty()) window->grabWindow().save(capture+"/modal-closed.png");
        loop.quit();
    });
    loop.exec();
    QVERIFY(nativeRaster); QVERIFY(fading); QVERIFY(settled); QVERIFY(captured>=4);
    // Reopening must refresh the snapshot and preserve the same fade path.
    QTimer::singleShot(0,popup,[popup]{QMetaObject::invokeMethod(popup,"open");});
    QTimer::singleShot(550,&loop,&QEventLoop::quit); loop.exec();
    QVERIFY(backdrop->property("active").toBool()); QVERIFY(backdrop->opacity()>.99);
}

void UiSmokeTests::dialogFramesAdvanceDuringRealEventLoop()
{
    pfui::AppInfo::registerQmlTypes(); QQmlApplicationEngine engine;
    auto* window = loadWindow(engine); QVERIFY(window);
    auto* popup = window->findChild<QObject*>("settingsDialog"); QVERIFY(popup);
    QElapsedTimer elapsed; elapsed.start();
    QList<qint64> timestamps;
    connect(window, &QQuickWindow::frameSwapped, this, [&] { timestamps.push_back(elapsed.elapsed()); }, Qt::QueuedConnection);
    QEventLoop loop;
    QTimer::singleShot(100, popup, [popup] { QMetaObject::invokeMethod(popup,"open"); });
    QTimer::singleShot(900, &loop, &QEventLoop::quit);
    loop.exec();
    QVERIFY(popup->property("opened").toBool());
    QVERIFY(popup->property("opacity").toDouble() > .99);
    QVERIFY(std::abs(popup->property("entranceOffset").toDouble()) < .1);
    QVERIFY(timestamps.size() >= 8);
    QList<qint64> gaps;
    for(int i=1;i<timestamps.size();++i) if(timestamps[i]>120 && timestamps[i]<500) gaps.push_back(timestamps[i]-timestamps[i-1]);
    std::sort(gaps.begin(),gaps.end());
    QVERIFY(!gaps.isEmpty());
    qInfo("Dialog frames: %lld; median interval: %lld ms", static_cast<long long>(timestamps.size()), static_cast<long long>(gaps[gaps.size()/2]));
    auto* tabs = popup->findChild<QQuickItem*>("settingsTabs"); QVERIFY(tabs);
    QCOMPARE(tabs->x(),0.0);
    QCOMPARE(tabs->width(),popup->property("width").toDouble());
}

void UiSmokeTests::slowSettingsWritesDoNotBlockUiAndKeepOtherSections()
{
    pfservices::SettingsStore::flushPendingWrites();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    pfservices::SettingsStore store((directory.path() + "/settings.json").toStdString());
    std::atomic<bool> entered{false}, release{false};
    struct ReleaseWorker {
        std::atomic<bool>& release;
        ~ReleaseWorker() { release = true; pfservices::SettingsStore::flushPendingWrites(); }
    } finish{release};
    store.updateAsync("slow-storage", [&](auto&) {
        entered = true;
        while (!release.load()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    });
    QTRY_VERIFY(entered.load());
    int ticks = 0;
    QTimer heartbeat; heartbeat.setInterval(10);
    connect(&heartbeat, &QTimer::timeout, [&] { ++ticks; }); heartbeat.start();
    for (int i = 0; i < 100; ++i) {
        store.updateAsync("appearance", [i](auto& settings) {
            settings.appearance.insert("surfaceOpacity", i / 100.0);
        });
        store.updateAsync("analysis", [i](auto& settings) { settings.processingThreads = i; });
    }
    QTest::qWait(150);
    QVERIFY2(ticks >= 5, "A blocked storage worker must not block the UI event loop");
    release = true;
    pfservices::SettingsStore::flushPendingWrites();
    std::string error;
    const auto saved = store.load(error);
    QCOMPARE(saved.appearance.value("surfaceOpacity").toDouble(), .99);
    QCOMPARE(saved.processingThreads, std::size_t(99));
    // A failed atomic commit must report the error and leave the worker able
    // to persist the next, independent file.
    QFile blocker(directory.path() + "/not-a-directory");
    QVERIFY(blocker.open(QIODevice::WriteOnly)); blocker.close();
    pfservices::SettingsStore invalid((blocker.fileName() + "/settings.json").toStdString());
    std::string writeError;
    invalid.updateAsync("appearance", [](auto& settings) { settings.language = "ru"; },
                        [&](std::string error) { writeError = std::move(error); });
    store.updateAsync("language", [](auto& settings) { settings.language = "ru"; });
    pfservices::SettingsStore::flushPendingWrites();
    QVERIFY(!writeError.empty());
    QCOMPARE(store.load(error).language, std::string("ru"));
}

void UiSmokeTests::appearanceAndVideoInspectionKeepUiResponsive()
{
    pfui::AppInfo::registerQmlTypes();
    QQmlApplicationEngine engine;
    auto* window = loadWindow(engine); QVERIFY(window);
    auto* popup = window->findChild<QObject*>("settingsDialog"); QVERIFY(popup);
    QVERIFY(QMetaObject::invokeMethod(popup, "open"));
    QTest::qWait(650);
    qInfo() << "Popup presentation" << popup->property("visible") << popup->property("opened")
            << popup->property("opacity") << popup->property("scale") << popup->property("entranceOffset")
            << window->isExposed() << window->isActive();
    QTRY_VERIFY(popup->property("opened").toBool());
    auto* tabs = popup->findChild<QObject*>("settingsTabs"); QVERIFY(tabs);
    tabs->setProperty("currentIndex", 1);
    QTest::qWait(550);
    QElapsedTimer elapsed; elapsed.start();
    qint64 last = 0, maxGap = 0;
    int ticks = 0;
    QTimer heartbeat; heartbeat.setInterval(10);
    connect(&heartbeat, &QTimer::timeout, [&] {
        const auto now = elapsed.elapsed(); maxGap = std::max(maxGap, now - last); last = now; ++ticks;
    }); heartbeat.start();
    const auto families = popup->property("fontChoices").toStringList();
    QVERIFY(!families.isEmpty());
    for (int i = 0; i < 12; ++i) {
        QVERIFY(QMetaObject::invokeMethod(popup, "applyFont", Q_ARG(QVariant, families[i % families.size()])));
        QVERIFY(QMetaObject::invokeMethod(popup, "applyAccent", Q_ARG(QVariant, QString(i % 2 ? "blue" : "orange"))));
        QTest::qWait(35);
    }
    // This opt-in path exercises the user's real scene pack without making
    // the ordinary suite depend on a private video collection.
    const auto video = qEnvironmentVariable("PF_UI_RESPONSIVENESS_VIDEO");
    if (!video.isEmpty()) {
        QVERIFY(QFileInfo::exists(video));
        QVERIFY(QMetaObject::invokeMethod(popup, "close"));
        auto* analysis = pfui::AnalysisController::instance();
        QVERIFY(QMetaObject::invokeMethod(window, "addFiles", Q_ARG(QVariant, QStringList{video})));
        QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy(), 30000);
        QCOMPARE(analysis->fileCount(), 1);
        window->setProperty("sourceFiles", QStringList{});
        analysis->inspectFiles({});
    }
    QTest::qWait(150);
    maxGap = std::max(maxGap, elapsed.elapsed() - last);
    qInfo("UI heartbeat: max gap %lld ms, %d ticks", static_cast<long long>(maxGap), ticks);
    QVERIFY2(ticks >= 10 && maxGap < 1500, "Appearance/video actions stalled the UI event loop");
    QVERIFY(QMetaObject::invokeMethod(popup, "resetAppearance"));
}

void UiSmokeTests::resultNavigationStopsAtEnds()
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
    QCOMPARE(rail->property("selectedIndex").toInt(), 1);
    QVERIFY(QMetaObject::invokeMethod(rail.get(), "selectPrevious"));
    QCOMPARE(rail->property("selectedIndex").toInt(), 2);
    QVERIFY(QMetaObject::invokeMethod(rail.get(), "selectPrevious"));
    QCOMPARE(rail->property("selectedIndex").toInt(), 2);
    QVERIFY(QMetaObject::invokeMethod(rail.get(), "selectNext"));
    QVERIFY(QMetaObject::invokeMethod(rail.get(), "goToTop"));
    QCOMPARE(rail->property("selectedIndex").toInt(), 2);
}


void UiSmokeTests::resultArrowKeysWorkAfterSourceButtonFocus()
{
    pfui::AppInfo::registerQmlTypes();
    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
    auto* window = loadWindow(engine);
    QVERIFY(window);
    auto* rail = window->findChild<QObject*>("resultsRail");
    auto* sources = window->findChild<QQuickItem*>("sourcesRail");
    QVERIFY(rail && sources);
    rail->setProperty("results", QVariantList{QVariantMap{{"id", 1}, {"similarity", .8}}, QVariantMap{{"id", 2}, {"similarity", .9}}});
    QSignalSpy selected(rail, SIGNAL(resultSelected(int)));
    QVERIFY(selected.isValid());
    QQmlComponent control(&engine);
    control.setData("import QtQuick\nimport QtQuick.Controls.Basic\nButton { text: 'Analyze' }", QUrl());
    std::unique_ptr<QQuickItem> button(qobject_cast<QQuickItem*>(control.create()));
    QVERIFY(button);
    button->setParentItem(sources);
    window->requestActivate();
    QTRY_VERIFY(window->isActive());
    for (const auto key : {Qt::Key_Down, Qt::Key_Up}) {
        const int initial = key == Qt::Key_Down ? 2 : 1;
        window->setProperty("selectedRecord", QVariantMap{{"id", initial}});
        window->setProperty("selectedResultIndex", initial);
        button->forceActiveFocus();
        QTRY_VERIFY(button->hasActiveFocus());
        const auto before = selected.count();
        QTest::keyClick(window, key);
        QTRY_COMPARE(selected.count(), before + 1);
        QCOMPARE(selected.last().first().toInt(), key == Qt::Key_Down ? 1 : 2);
    }
    // Sorting must never steal arrows, even before the user clicks a card.
    auto* sort = window->findChild<QQuickItem*>("resultSortCombo");
    QVERIFY(sort);
    const auto sortIndex = sort->property("currentIndex").toInt();
    window->setProperty("selectedRecord", QVariantMap{{"id", 2}});
    window->setProperty("selectedResultIndex", 2);
    sort->forceActiveFocus();
    auto selectedBefore = selected.count();
    QTest::keyClick(window, Qt::Key_Down);
    QTRY_COMPARE(selected.count(), selectedBefore + 1);
    QCOMPARE(selected.last().first().toInt(), 1);
    QCOMPARE(sort->property("currentIndex").toInt(), sortIndex);
    for (const auto key : {Qt::Key_Left, Qt::Key_Right}) {
        sort->forceActiveFocus();
        QTest::keyClick(window, key);
        QCOMPARE(sort->property("currentIndex").toInt(), sortIndex);
    }
    // Typing and modal settings retain ownership of their arrow keys.
    control.setData("import QtQuick\nimport QtQuick.Controls.Basic\nTextField { text: 'abc' }", QUrl());
    std::unique_ptr<QQuickItem> field(qobject_cast<QQuickItem*>(control.create()));
    QVERIFY(field);
    field->setParentItem(sources);
    window->setProperty("selectedRecord", QVariantMap{{"id", 2}});
    field->forceActiveFocus();
    const auto before = selected.count();
    QTest::keyClick(window, Qt::Key_Down);
    QCOMPARE(selected.count(), before);
    auto* settings = window->findChild<QObject*>("settingsDialog");
    QVERIFY(settings);
    QVERIFY(QMetaObject::invokeMethod(settings, "open"));
    QTRY_VERIFY(settings->property("opened").toBool());
    QTest::keyClick(window, Qt::Key_Down);
    QCOMPARE(selected.count(), before);
}

void UiSmokeTests::selectsAllResultsWithoutDisplayLimit()
{
    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nimport PfUi\nResultsRail { width: 280; height: 500; onExportSelectionChanged: function(rows) { selectedRows = rows } }", QUrl());
    std::unique_ptr<QObject> rail(component.create());
    QVERIFY2(rail != nullptr, qPrintable(component.errorString()));
    QVariantList rows;
    for (int i = 0; i < 200; ++i) rows.push_back(QVariantMap{{"id", i}, {"similarity", 0.8}});
    rail->setProperty("results", rows);
    QVERIFY(QMetaObject::invokeMethod(rail.get(), "selectAll"));
    const auto selection = rail->property("selectedRows").value<QJSValue>().toVariant().toMap();
    QCOMPARE(selection.size(), 200);
    QVERIFY(selection.contains("0"));
    QVERIFY(selection.contains("199"));
    QVERIFY(rail->findChild<QObject*>("clearResultsSelectionButton")->property("enabled").toBool());
    QVERIFY(!rail->findChild<QObject*>("selectAllResultsButton")->property("enabled").toBool());
    auto* sort = rail->findChild<QObject*>("resultSortCombo");
    QVERIFY(sort);
    sort->setProperty("currentIndex", 1);
    QVERIFY(QMetaObject::invokeMethod(sort, "activated", Q_ARG(int, 1)));
    QVERIFY(!rail->property("sortDescending").toBool());
    sort->setProperty("currentIndex", 4);
    QVERIFY(QMetaObject::invokeMethod(sort, "activated", Q_ARG(int, 4)));
    QCOMPARE(rail->property("sortCriterion").toString(), QStringLiteral("time"));
    for (int i = 0; i < 6; ++i) {
        sort->setProperty("currentIndex", i);
        QVERIFY(QMetaObject::invokeMethod(sort, "activated", Q_ARG(int, i)));
        QCOMPARE(rail->property("sortCriterion").toString(), i >= 4 ? QStringLiteral("time") : i >= 2 ? QStringLiteral("scene") : QStringLiteral("movement"));
        QCOMPARE(rail->property("sortDescending").toBool(), i < 4 ? i % 2 == 0 : i == 5);
    }
    QVERIFY(QMetaObject::invokeMethod(rail.get(), "clearSelection"));
    QCOMPARE(rail->property("selectedRows").value<QJSValue>().toVariant().toMap().size(), 0);
    QVERIFY(!rail->findChild<QObject*>("clearResultsSelectionButton")->property("enabled").toBool());
}

void UiSmokeTests::exportModesAreSelectable()
{
    pfui::AppInfo::registerQmlTypes();
    QQmlApplicationEngine engine;
    auto* window = loadWindow(engine);
    QVERIFY(window);
    auto* popup = window->findChild<QObject*>("exportDialog");
    QVERIFY(popup);
    QCOMPARE(popup->property("selectedFormat").toString(), QStringLiteral("FFMPEG"));
    QCOMPARE(popup->property("fileBaseName").toString(), QStringLiteral("frame"));
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
    auto* merge = popup->findChild<QObject*>("mergeChronologicalCheck");
    QVERIFY(merge);
    merge->setProperty("checked", true);
    QVERIFY(popup->property("mergeChronological").toBool());
    QCOMPARE(popup->property("selectedCutMode").toInt(), 0);
    QVERIFY(!popup->findChild<QQuickItem*>("fastCutButton")->isEnabled());
    const auto capture = qEnvironmentVariable("PF_UI_CAPTURE_DIR");
    if (!capture.isEmpty()) { QDir().mkpath(capture); QTest::qWait(150); QVERIFY(window->grabWindow().save(capture + "/montage-export.png")); }
    merge->setProperty("checked", false);
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

void UiSmokeTests::matcherStagesTranslateWithActualCounters()
{
    pfui::AppInfo::registerQmlTypes();QQmlEngine engine;engine.addImportPath("qrc:/qt/qml");
    QQmlComponent component(&engine);
    component.setData(R"(import QtQuick
import PfUi
Item {
    property string stage: "Матчер: Сравниваем найденные движения · 125 / 1000"
    property string translated: L10n.status(stage)
    function setLanguage(value) { L10n.language = value }
})",QUrl());
    std::unique_ptr<QObject> root(component.create());QVERIFY2(root,qPrintable(component.errorString()));
    QVERIFY(QMetaObject::invokeMethod(root.get(),"setLanguage",Q_ARG(QVariant,"ru")));
    QCOMPARE(root->property("translated").toString(),QString::fromUtf8("Матчер: Сравниваем найденные движения · 125 / 1000"));
    QVERIFY(QMetaObject::invokeMethod(root.get(),"setLanguage",Q_ARG(QVariant,"en")));
    QCOMPARE(root->property("translated").toString(),QStringLiteral("Matcher: Comparing candidate movements · 125 / 1000"));
    root->setProperty("stage",QString::fromUtf8("Матчер: Ищем совпадения персонажа · 512 / 1200"));
    QCOMPARE(root->property("translated").toString(),QStringLiteral("Matcher: Finding character matches · 512 / 1200"));
    root->setProperty("stage",QString::fromUtf8("Матчер: Подготавливаем найденные кандидаты · 2048 / 9000"));
    QCOMPARE(root->property("translated").toString(),QStringLiteral("Matcher: Preparing retrieved candidates · 2048 / 9000"));
    root->setProperty("stage",QString::fromUtf8("Подготавливаем кадры для матчера · 40 / 90"));
    QCOMPARE(root->property("translated").toString(),QStringLiteral("Preparing footage for matching · 40 / 90"));
    QVERIFY(QMetaObject::invokeMethod(root.get(),"setLanguage",Q_ARG(QVariant,"ru")));
}

void UiSmokeTests::resultLabelsFollowMatchTypeAndLanguage()
{
    pfui::AppInfo::registerQmlTypes();
    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
    QQmlComponent component(&engine);
    component.setData(R"(
import QtQuick
import PfUi
Item {
    width: 1200; height: 700
    property var sample: ({id: 0, matchType: 'pose', similarity: 0.91})
    function setLanguage(value) { L10n.language = value }
    MotionCenter { objectName: "testedCenter"; width: 850; height: 700; selectedRecord: sample }
    ResultsRail { objectName: "testedRail"; x: 850; width: 350; height: 700; results: [sample] }
})", QUrl());
    std::unique_ptr<QObject> root(component.create());
    QVERIFY2(root != nullptr, qPrintable(component.errorString()));
    auto* center = root->findChild<QQuickItem*>("testedCenter");
    auto* rail = root->findChild<QQuickItem*>("testedRail");
    QVERIFY(center && rail);
    std::function<bool(QQuickItem*, const QString&)> containsLabel =
        [&](QQuickItem* item, const QString& prefix) {
            if (item->property("text").toString().startsWith(prefix)) return true;
            for (auto* child : item->childItems())
                if (containsLabel(child, prefix)) return true;
            return false;
        };
    for (const auto& language : {QStringLiteral("en"), QStringLiteral("ru")}) {
        QVERIFY(QMetaObject::invokeMethod(root.get(), "setLanguage", Q_ARG(QVariant, language)));
        for (const bool motion : {false, true}) {
            root->setProperty("sample", QVariantMap{{"id", 0}, {"matchType", motion ? "motion" : "pose"}, {"similarity", 0.91}});
            const QString expected = language == "en"
                ? (motion ? "Motion similarity" : "Pose similarity")
                : QString::fromUtf8(motion ? "Сходство движения" : "Сходство позы");
            QTRY_VERIFY(containsLabel(center, expected + QStringLiteral(" · 91%")));
            QTRY_VERIFY(containsLabel(rail, expected + QStringLiteral(" · 91%")));
        }
    }
}

void UiSmokeTests::staticResultsShowPlaybackControls()
{
    pfui::AppInfo::registerQmlTypes();
    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nimport PfUi\nComparisonView { width: 300; height: 500; record: ({matchType: 'pose', leftSource: 'C:/video.mp4', leftStart: 1, leftEnd: 1, leftSceneEnd: 3}) }", QUrl());
    std::unique_ptr<QObject> view(component.create());
    QVERIFY2(view != nullptr, qPrintable(component.errorString()));
    auto* play = view->findChild<QObject*>("previewPlayButton");
    QVERIFY(play);
    QVERIFY(play->property("visible").toBool());
    QVERIFY(play->property("enabled").toBool());
    auto* playItem = qobject_cast<QQuickItem*>(play);
    QVERIFY(playItem);
    auto* panelItem = qobject_cast<QQuickItem*>(view.get());
    QVERIFY(panelItem);
    const auto position = playItem->mapToItem(panelItem, QPointF(0, 0));
    QVERIFY2(position.y() + playItem->height() <= panelItem->height(),
             "Preview button must remain inside the visible comparison panel");
    QCOMPARE(view->property("playbackEnd").toDouble(), 3.0);
    view->setProperty("record", QVariantMap{{"matchType", "motion"}, {"leftStart", 1}, {"leftEnd", 2}});
    QVERIFY(play->property("visible").toBool());

    QQmlComponent centerComponent(&engine);
    centerComponent.setData(R"(
import QtQuick
import PfUi
MotionCenter {
    width: 800; height: 650
    selectedRecord: ({matchType: 'pose', leftSource: 'C:/a.mp4', rightSource: 'C:/b.mp4',
                      leftStart: 1, rightStart: 2, leftSceneEnd: 3, rightSceneEnd: 4})
})", QUrl());
    std::unique_ptr<QObject> center(centerComponent.create());
    QVERIFY2(center != nullptr, qPrintable(centerComponent.errorString()));
    auto* left = center->findChild<QObject*>("leftComparison");
    auto* right = center->findChild<QObject*>("rightComparison");
    auto* pair = center->findChild<QObject*>("pairPlaybackButton");
    QVERIFY(left && right && pair);
    auto* leftPlay = left->findChild<QObject*>("previewPlayButton");
    auto* rightPlay = right->findChild<QObject*>("previewPlayButton");
    QVERIFY(leftPlay && rightPlay);
    QVERIFY(leftPlay->property("visible").toBool());
    QVERIFY(rightPlay->property("visible").toBool());
    QVERIFY(pair->property("visible").toBool());
}

void UiSmokeTests::previewIsEmbeddedAndStopsOnRecordChange()
{
    pfui::AppInfo::registerQmlTypes();
    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nimport PfUi\nComparisonView { width: 300; height: 500 }", QUrl());
    std::unique_ptr<QObject> view(component.create());
    QVERIFY2(view != nullptr, qPrintable(component.errorString()));
    QVERIFY(!view->findChild<QObject*>("inlineVideoOutput"));
    // Opening an empty workspace must not initialize two unused decoders
    // and audio device backends before the first frame of the window.
    QVERIFY(!view->findChild<QObject*>("inlineMediaPlayer"));
    view->setProperty("videoMode", true);
    view->setProperty("record", QVariantMap{{"matchType", "motion"}, {"leftStart", 1}, {"leftEnd", 2}});
    QVERIFY(!view->findChild<QObject*>("inlineMediaPlayer"));
    QVERIFY(!view->findChild<QObject*>("inlineVideoOutput"));
    QVERIFY(!view->property("videoMode").toBool());
    view->setProperty("record", QVariantMap{{"matchType", "motion"}, {"leftStart", 1},
        {"leftEnd", 3}, {"leftSceneEnd", 2}});
    QCOMPARE(view->property("playbackEnd").toDouble(), 2.0);
    view->setProperty("record", QVariantMap{{"matchType", "motion"}, {"leftStart", 10},
        {"leftEnd", 12}, {"leftSceneStart", 0}, {"leftSceneEnd", 60},
        {"leftClipStart", 8.5}, {"leftClipEnd", 13.5}});
    QCOMPARE(view->property("playbackStart").toDouble(), 8.5);
    QCOMPARE(view->property("playbackEnd").toDouble(), 13.5);
    view->setProperty("record", QVariant());
    QTRY_VERIFY(!view->findChild<QObject*>("inlineMediaPlayer"));
}

void UiSmokeTests::inlinePairActuallyDecodesAndStopsAtClipEnd()
{
    const auto ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    if (ffmpeg.isEmpty()) QSKIP("FFmpeg is required for the real playback fixture");
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath(QString::fromUtf8("видео #1.mp4"));
    QProcess encoder;
    encoder.start(ffmpeg, {"-v", "error", "-f", "lavfi", "-i", "testsrc2=size=160x90:rate=15", "-t", "3", "-c:v", "libx264", "-pix_fmt", "yuv420p", path});
    QVERIFY(encoder.waitForFinished(30000));
    QCOMPARE(encoder.exitCode(), 0);
    pfui::AppInfo::registerQmlTypes();
    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nimport PfUi\nMotionCenter { width: 900; height: 600 }", QUrl());
    std::unique_ptr<QObject> center(component.create());
    QVERIFY2(center != nullptr, qPrintable(component.errorString()));
    center->setProperty("selectedRecord", QVariantMap{{"matchType", "motion"}, {"leftSource", path}, {"rightSource", path},
        {"leftStart", 0.7}, {"leftEnd", 0.9}, {"rightStart", 1.9}, {"rightEnd", 2.1},
        {"leftClipStart", 0.3}, {"leftClipEnd", 1.3}, {"rightClipStart", 1.5}, {"rightClipEnd", 2.5}});
    auto* left = center->findChild<QObject*>("leftComparison");
    auto* right = center->findChild<QObject*>("rightComparison");
    QVERIFY(left && right);
    QVERIFY(!left->findChild<QMediaPlayer*>("inlineMediaPlayer"));
    QVERIFY(!right->findChild<QMediaPlayer*>("inlineMediaPlayer"));
    // Per-panel buttons must not be aliases for the shared A/B control.
    for (auto* panel : {right, left}) {
        auto* other = panel == right ? left : right;
        auto* button = panel->findChild<QObject*>("previewPlayButton");
        QVERIFY(button);
        QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
        QTRY_VERIFY_WITH_TIMEOUT(panel->property("playing").toBool(), 15000);
        QVERIFY(!other->property("videoMode").toBool());
        QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
        QTRY_VERIFY(!panel->property("playing").toBool());
        QVERIFY(panel->property("videoMode").toBool());
        QVERIFY(QMetaObject::invokeMethod(button, "clicked"));
        QTRY_VERIFY_WITH_TIMEOUT(!panel->property("videoMode").toBool(), 10000);
        QVERIFY(!other->property("videoMode").toBool());
    }
    auto* leftPlayer = left->findChild<QMediaPlayer*>("inlineMediaPlayer");
    auto* rightPlayer = right->findChild<QMediaPlayer*>("inlineMediaPlayer");
    QVERIFY(leftPlayer && rightPlayer);
    QVERIFY(leftPlayer->videoSink() && rightPlayer->videoSink());
    QSignalSpy leftFrames(leftPlayer->videoSink(), &QVideoSink::videoFrameChanged);
    QSignalSpy rightFrames(rightPlayer->videoSink(), &QVideoSink::videoFrameChanged);
    QVERIFY(QMetaObject::invokeMethod(center.get(), "startPair"));
    QTRY_VERIFY_WITH_TIMEOUT(leftFrames.count() > 0 && rightFrames.count() > 0, 15000);
    QVERIFY(center->property("playbackError").toString().isEmpty());
    QVERIFY(QMetaObject::invokeMethod(left->findChild<QObject*>("previewPlayButton"), "clicked"));
    QTRY_VERIFY(!left->property("playing").toBool());
    QVERIFY(left->property("videoMode").toBool());
    QVERIFY(right->property("playing").toBool());
    QVERIFY(QMetaObject::invokeMethod(center.get(), "startPair"));
    QTRY_VERIFY_WITH_TIMEOUT(!left->property("videoMode").toBool() && !right->property("videoMode").toBool(), 10000);
    QVERIFY(!center->property("pendingPlayback").toBool());
    // Independent players stay independent at clip end, not just at startup.
    center->setProperty("selectedRecord", QVariantMap{{"matchType", "motion"}, {"leftSource", path}, {"rightSource", path},
        {"leftStart", 0.0}, {"leftEnd", 2.8}, {"rightStart", 1.5}, {"rightEnd", 2.0}});
    QVERIFY(QMetaObject::invokeMethod(left->findChild<QObject*>("previewPlayButton"), "clicked"));
    QTRY_VERIFY_WITH_TIMEOUT(left->property("playing").toBool(), 15000);
    QVERIFY(QMetaObject::invokeMethod(right->findChild<QObject*>("previewPlayButton"), "clicked"));
    QTRY_VERIFY_WITH_TIMEOUT(right->property("playing").toBool(), 15000);
    QTRY_VERIFY_WITH_TIMEOUT(!right->property("videoMode").toBool(), 10000);
    QVERIFY(left->property("playing").toBool());
    center->setProperty("selectedRecord", QVariantMap{{"matchType", "pose"}, {"id", 99}});
    QVERIFY(!left->property("videoMode").toBool());
    QVERIFY(!right->property("videoMode").toBool());
    center->setProperty("selectedRecord", QVariantMap{{"matchType", "pose"}, {"leftSource", path}, {"rightSource", path},
        {"leftStart", 0.3}, {"leftEnd", 0.3}, {"leftSceneEnd", 1.3},
        {"rightStart", 1.5}, {"rightEnd", 1.5}, {"rightSceneEnd", 2.5}});
    auto* staticPlay = left->findChild<QObject*>("previewPlayButton");
    QVERIFY(staticPlay->property("visible").toBool());
    QVERIFY(staticPlay->property("enabled").toBool());
    QVERIFY(QMetaObject::invokeMethod(staticPlay, "clicked"));
    QTRY_VERIFY_WITH_TIMEOUT(left->property("playing").toBool(), 15000);
    QTRY_VERIFY_WITH_TIMEOUT(!left->property("videoMode").toBool(), 10000);
    QVERIFY(!right->property("videoMode").toBool());
}

void UiSmokeTests::cacheFolderAcceptsLocalFileUrls()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto* analysis = pfui::AnalysisController::instance();
    const auto original = analysis->cachePath();
    const auto path = directory.filePath(QString::fromUtf8("кэш с пробелами"));
    analysis->setCachePath(QUrl::fromLocalFile(path).toString());
    const auto actual = analysis->cachePath();
    analysis->setCachePath(original);
    QCOMPARE(actual, path);
}

void UiSmokeTests::sourceStatisticsFollowSelectionAndInspectionScope()
{
    pfui::AppInfo::registerQmlTypes();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    QVERIFY(!ffmpeg.isEmpty());
    QStringList paths;
    for (int seconds : {1, 2}) {
        const auto path = directory.filePath(QString("video-%1.mp4").arg(seconds));
        QProcess generator;
        generator.start(ffmpeg, {"-v", "error", "-f", "lavfi", "-i",
            QString("testsrc2=size=32x32:rate=10:duration=%1").arg(seconds),
            "-c:v", "libx264", "-y", path});
        QVERIFY(generator.waitForFinished(30000));
        QCOMPARE(generator.exitCode(), 0);
        paths.push_back(path);
    }
    paths.insert(1, directory.filePath("missing.mp4"));
    QQmlApplicationEngine engine;
    auto* window = loadWindow(engine);
    QVERIFY(window);
    window->setProperty("sourceFiles", paths);
    auto* analysis = pfui::AnalysisController::instance();
    analysis->inspectFiles(paths);
    QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy(), 15000);
    QCOMPARE(analysis->fileCount(), 2);
    QCOMPARE(analysis->frameCount(), 30);
    QCOMPARE(analysis->durationSeconds(), 3.0);
    auto* rail = window->findChild<QObject*>("sourcesRail");
    auto* strip = window->findChild<QObject*>("statsStrip");
    QVERIFY(rail && strip);
    rail->setProperty("selectedSourceIndex", 2);
    QTRY_COMPARE(strip->property("selectedSource").toString(), paths[2]);
    const auto summary = [strip] {
        const auto value = strip->property("summary");
        return value.metaType() == QMetaType::fromType<QJSValue>()
            ? value.value<QJSValue>().toVariant().toMap() : value.toMap();
    };
    QTRY_COMPARE(summary().value("frameCount").toLongLong(), 20);
    QCOMPARE(summary().value("durationSeconds").toDouble(), 2.0);
    rail->setProperty("selectedSourceIndex", 0);
    QTRY_COMPARE(summary().value("frameCount").toLongLong(), 10);
    rail->setProperty("selectedSourceIndex", 1);
    QTRY_COMPARE(summary().value("fileCount").toInt(), 0);
    rail->setProperty("selectedSourceIndex", -1);
    QTRY_COMPARE(strip->property("selectedSource").toString(), QString());
    // Replacing inspection while it runs must never publish the stale subset.
    analysis->inspectFiles({paths[0]});
    analysis->inspectFiles({paths[0], paths[2]});
    QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy(), 15000);
    QCOMPARE(analysis->fileCount(), 2);
    QCOMPARE(analysis->frameCount(), 30);
    analysis->inspectFiles({paths[2]});
    QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy(), 15000);
    QCOMPARE(analysis->fileCount(), 1);
    QCOMPARE(analysis->frameCount(), 20);
    analysis->inspectFiles({});
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

int main(int argc, char** argv)
{
    if (argc>2 && std::strcmp(argv[1],"--pf-test-backend-child")==0) {
        QCoreApplication app(argc,argv);
        const auto mode=app.arguments()[2];
        if(mode=="failure") return 3;
        if(mode=="slow") std::this_thread::sleep_for(std::chrono::seconds(10));
        if(mode=="invalid") { std::printf("PF_BACKENDS_JSON=invalid\n"); return 0; }
        pfgpu::BackendProbe probe; probe.ortLoaded=true; probe.ortVersion="test-runtime";
        probe.backends={{pfgpu::Provider::Cpu,true,"","Test CPU"}};
        std::printf("PF_BACKENDS_JSON=%s\n",pfui::backendProbeJson(probe).constData()); return 0;
    }
    pfui::configureUiRuntime();
    QGuiApplication app(argc, argv);
    UiSmokeTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "test_ui_smoke.moc"
