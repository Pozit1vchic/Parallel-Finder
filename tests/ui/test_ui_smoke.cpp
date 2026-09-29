// QTest smoke for the static QML module PfUi: Main.qml loads from resources,
// Theme/L10n singletons resolve, AppInfo bridge is registered.
#include <QtGui/QColor>
#include <QtGui/QGuiApplication>
#include <QtGui/QFontMetricsF>
#include <QtGui/QFontDatabase>
#include <QtCore/QDir>
#include <QtQml/QQmlApplicationEngine>
#include <QtQml/QQmlComponent>
#include <QtQuick/QQuickWindow>
#include <QtQuick/QQuickItem>
#include <QtTest/QTest>

#include <AppInfo.h>
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

class UiSmokeTests : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { QCoreApplication::setOrganizationName("ParallelFinderTests"); QCoreApplication::setOrganizationDomain("test.invalid"); QCoreApplication::setApplicationName("UiAudit"); }
    void mainQmlLoadsFromResources();
    void themeSingletonResolves();
    void appInfoBridgeResolves();
    void gpuInfoPropagatesToQml();
    void settingsAndNumericTypography();
    void appearancePersistsAndRejectsMissingFonts();
    void resultNavigationWrapsAndScrolls();
    void resultArrowKeysWorkAfterSourceButtonFocus();
    void selectsAllResultsWithoutDisplayLimit();
    void exportModesAreSelectable();
    void advancedOpensOnFirstClickAndStatusTranslates();
    void directMlDownloadIntegration();
    void cacheFolderAcceptsLocalFileUrls();
    void staticResultsHidePlaybackControls();
    void previewIsEmbeddedAndStopsOnRecordChange();
    void inlinePairActuallyDecodesAndStopsAtClipEnd();
    void resultLabelsFollowMatchTypeAndLanguage();
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
        window->setProperty("selectedRecord", QVariantMap{{"id", 2}});
        window->setProperty("selectedResultIndex", 2);
        button->forceActiveFocus();
        QTRY_VERIFY(button->hasActiveFocus());
        const auto before = selected.count();
        QTest::keyClick(window, key);
        QTRY_COMPARE(selected.count(), before + 1);
        QCOMPARE(selected.last().first().toInt(), 1);
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
    QVERIFY(QMetaObject::invokeMethod(rail.get(), "clearSelection"));
    QCOMPARE(rail->property("selectedRows").value<QJSValue>().toVariant().toMap().size(), 0);
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

void UiSmokeTests::staticResultsHidePlaybackControls()
{
    pfui::AppInfo::registerQmlTypes();
    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nimport PfUi\nComparisonView { width: 300; height: 500; record: ({matchType: 'pose', leftStart: 1, leftEnd: 2}) }", QUrl());
    std::unique_ptr<QObject> view(component.create());
    QVERIFY2(view != nullptr, qPrintable(component.errorString()));
    auto* play = view->findChild<QObject*>("previewPlayButton");
    QVERIFY(play);
    QVERIFY(!play->property("visible").toBool());
    view->setProperty("record", QVariantMap{{"matchType", "motion"}, {"leftStart", 1}, {"leftEnd", 2}});
    QVERIFY(play->property("visible").toBool());
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
    QVERIFY(view->findChild<QObject*>("inlineVideoOutput"));
    QVERIFY(view->findChild<QObject*>("inlineMediaPlayer"));
    view->setProperty("videoMode", true);
    view->setProperty("record", QVariantMap{{"matchType", "motion"}, {"leftStart", 1}, {"leftEnd", 2}});
    QVERIFY(!view->property("videoMode").toBool());
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
        {"leftStart", 0.3}, {"leftEnd", 1.3}, {"rightStart", 1.5}, {"rightEnd", 2.5}});
    auto* left = center->findChild<QObject*>("leftComparison");
    auto* right = center->findChild<QObject*>("rightComparison");
    QVERIFY(left && right);
    auto* leftPlayer = left->findChild<QMediaPlayer*>("inlineMediaPlayer");
    auto* rightPlayer = right->findChild<QMediaPlayer*>("inlineMediaPlayer");
    QVERIFY(leftPlayer && rightPlayer);
    QVERIFY(leftPlayer->videoSink() && rightPlayer->videoSink());
    QSignalSpy leftFrames(leftPlayer->videoSink(), &QVideoSink::videoFrameChanged);
    QSignalSpy rightFrames(rightPlayer->videoSink(), &QVideoSink::videoFrameChanged);
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
    leftFrames.clear();
    rightFrames.clear();
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
