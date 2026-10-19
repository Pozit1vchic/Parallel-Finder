#include <pfcore/VideoDecoder.hpp>
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
#include <McpController.h>
#include <ResultClassification.h>
#include <PreviewRequestOrder.h>
#include <QTcpServer>
#include <QClipboard>
#include <QTemporaryDir>
#include <pfservices/SettingsStore.hpp>
#include <QFileInfo>
#include <QRegularExpression>
#include <QProcess>
#include <QStandardPaths>
#include <QMediaPlayer>
#include <QVideoSink>
#include <QSignalSpy>
#include <functional>
#include <cmath>
#include <atomic>
#include <QElapsedTimer>
#include <QTimer>
#include <QScopeGuard>
#include <DiscordPresence.h>
#include <pfgpu/SessionCache.hpp>
#if defined(Q_OS_WIN)
#include <windows.h>
#include <psapi.h>
#endif

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
    void manualSortAndComposeDialogs();
    void realSoldierDefaultDiscoveryColdAndWarm();
    void realAnalysisPublishesProgressWithoutDiagnostics();
    void reviewShortcutsAndMultiSelection();
    void realReviewAdjustAndFindings();
    void realAutomaticClassConfirmationSurvivesTrim();
    void realWalkingClassificationTrimAndOrderedExport();
    void idleWindowAndDiscordMemoryPlateau();
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
    void languageTabOffersSixPersistentChoices();
    void rc23PresetsTranslationsAndFlagMotion();
    void customAccentPaletteUpdatesImmediatelyAndPersists();
    void slowSettingsWritesDoNotBlockUiAndKeepOtherSections();
    void appearanceAndVideoInspectionKeepUiResponsive();
    void resultNavigationStopsAtEnds();
    void reviewRemovalKeepsNeighborAndColorsRefresh();
    void sourceSearchAndUnreviewedNavigation();
    void sourcesLiveInsideDropAreaAboveActions();
    void sourceWheelDoesNotMoveSettingsRail();
    void movementClassesAndExportOrder();
    void classificationsFollowEditedRange();
    void previewRequestsRenderBySourceAndTimeWithoutReindexing();
    void staticPoseDisagreementKeepsBothPoses();
    void largeResultSetKeepsClassesContiguous();
    void mcpSettingsExposeWorkingConnection();
    void exportClassOrderControlsFileNumbering();
    void dialogFramesAdvanceDuringRealEventLoop();
    void modalCloseKeepsNativeBordersAndFades();
    void appearanceRefreshesBackdropBeforeClosing();
    void settingsDragAndScrollKeepRendering();
    void finiteAnimationsRespectReducedMotion();
    void idleWorkspaceStopsRequestingFrames();
    void resultArrowKeysWorkAfterSourceButtonFocus();
    void selectsAllResultsWithoutDisplayLimit();
    void exportModesAreSelectable();
    void exportReopenClearsCancelledStatus();
    void movementFilterRespectsSelectionAndVisibleRows();
    void largeExportBackdropKeepsWorkspaceGeometry();
    void analysisControlsAreUnifiedAndStatusTranslates();
    void directMlDownloadIntegration();
    void cacheFolderAcceptsLocalFileUrls();
    void staticResultsShowPlaybackControls();
    void previewIsEmbeddedAndStopsOnRecordChange();
    void inlinePairActuallyDecodesAndStopsAtClipEnd();
    void matcherStagesTranslateWithActualCounters();
    void resultLabelsFollowMatchTypeAndLanguage();
    void sourceStatisticsFollowSelectionAndInspectionScope();
};

void UiSmokeTests::realAnalysisPublishesProgressWithoutDiagnostics()
{
    const auto source = qEnvironmentVariable("PF_REAL_PROGRESS_SOURCE");
    if (source.isEmpty()) QSKIP("Opt-in real-video progress regression");
    QVERIFY(QFileInfo::exists(source));
    const auto debug = qgetenv("PF_DEBUG_ANALYSIS");
    const bool debugWasSet = qEnvironmentVariableIsSet("PF_DEBUG_ANALYSIS");
    qunsetenv("PF_DEBUG_ANALYSIS");
    const auto restoreDebug = qScopeGuard([&] {
        if (debugWasSet) qputenv("PF_DEBUG_ANALYSIS", debug);
        else qunsetenv("PF_DEBUG_ANALYSIS");
    });
    auto* analysis = pfui::AnalysisController::instance();
    analysis->setModelPath(qEnvironmentVariable("PF_MODEL_PATH"));
    analysis->setProviderChoice("cuda");
    analysis->setQualityProfile("fast");
    analysis->setAccuracyPreset("fast");
    analysis->setExpandedSearch(true);
    QElapsedTimer elapsed;
    elapsed.start();
    qint64 firstProgressMs = -1;
    const auto connection = connect(analysis, &pfui::AnalysisController::progressChanged, this, [&] {
        if (analysis->busy() && analysis->progress() > 0 && analysis->progress() < .85
            && analysis->progressStage() == QStringLiteral("Анализируем движение")
            && firstProgressMs < 0) {
            firstProgressMs = elapsed.elapsed();
            analysis->stopAnalysis();
        }
    });
    const auto disconnectProgress = qScopeGuard([&] { disconnect(connection); });
    analysis->analyzeFiles({source});
    QTRY_VERIFY_WITH_TIMEOUT(firstProgressMs >= 0 || !analysis->busy(), 120000);
    if (firstProgressMs < 0) analysis->stopAnalysis();
    QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy(), 30000);
    QVERIFY2(firstProgressMs >= 0, "Normal analysis did not publish intermediate decode progress");
    qInfo("PF_REAL_FIRST_PROGRESS elapsed_ms=%lld source=%s diagnostics=disabled",
        static_cast<long long>(firstProgressMs), qPrintable(source));
}

void UiSmokeTests::realSoldierDefaultDiscoveryColdAndWarm()
{
    const auto source = qEnvironmentVariable("PF_REAL_SOLDIER_SOURCE");
    if (source.isEmpty()) QSKIP("Opt-in real-video CUDA regression");
    QVERIFY(QFileInfo::exists(source));
    QVERIFY(qEnvironmentVariableIsEmpty("PF_MODEL_ROOT"));
    QVERIFY(qEnvironmentVariableIsEmpty("PF_REID_MODEL_PATH"));
    auto* analysis = pfui::AnalysisController::instance();
    analysis->setModelPath(qEnvironmentVariable("PF_MODEL_PATH"));
    analysis->setProviderChoice("cuda"); analysis->setQualityProfile("fast");
    analysis->setAnalysisMode("combined"); analysis->setAccuracyPreset("fast");
    analysis->setExpandedSearch(true);
    analysis->analyzeFiles({source});
    QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy(), 300000);
    QVERIFY2(analysis->analysisCompleted(), qPrintable(analysis->status()));
    QVERIFY2(analysis->matchCount() >= 5, qPrintable(analysis->status()));
    const auto cold = analysis->results();
    qInfo("PF_REAL_SOLDIER cold_pairs=%d", analysis->matchCount());
    pfui::AppInfo::registerQmlTypes();
    QQmlApplicationEngine engine;
    engine.load(QUrl("qrc:/qt/qml/PfUi/qml/Main.qml"));
    QVERIFY(!engine.rootObjects().isEmpty());
    auto* window = engine.rootObjects().constFirst();
    const int firstId = cold.constFirst().toMap().value("id").toInt();
    QTRY_COMPARE(window->property("selectedResultIndex").toInt(), firstId);
    qputenv("PF_AUDIT_REUSE_OBSERVATIONS", "1");
    analysis->analyzeFiles({source});
    QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy(), 300000);
    QVERIFY2(analysis->analysisCompleted(), qPrintable(analysis->status()));
    QCOMPARE(analysis->matchCount(), cold.size());
    auto stripPreviews = [](QVariantList values) {
        for (auto& value : values) {
            auto row = value.toMap(); row.remove("leftPreview"); row.remove("rightPreview"); value = row;
        }
        return values;
    };
    QCOMPARE(stripPreviews(analysis->results()), stripPreviews(cold));
    QTRY_COMPARE(window->property("selectedResultIndex").toInt(), firstId);
    auto* center = window->findChild<QObject*>("motionCenter");
    QVERIFY(center);
    QCOMPARE(center->property("availableResultCount").toInt(), cold.size());
    qInfo("PF_REAL_SOLDIER warm_pairs=%d", analysis->matchCount());
    qunsetenv("PF_AUDIT_REUSE_OBSERVATIONS");
}

void UiSmokeTests::reviewShortcutsAndMultiSelection()
{
    pfui::AppInfo::registerQmlTypes();
    QQmlApplicationEngine engine;
    engine.load(QUrl("qrc:/qt/qml/PfUi/qml/Main.qml"));
    QVERIFY(!engine.rootObjects().isEmpty());
    auto* review = engine.singletonInstance<QObject*>(qmlTypeId("PfUi", 1, 0, "Review"));
    QVERIFY(review);
    const auto variant = [](const QVariant& value) { return value.metaType() == QMetaType::fromType<QJSValue>() ? value.value<QJSValue>().toVariant() : value; };
    QVERIFY(QMetaObject::invokeMethod(review, "reset"));
    bool hasManualReviewedAction = false;
    for (const auto& action : variant(review->property("actions")).toList())
        hasManualReviewedAction = hasManualReviewedAction || action.toMap().value("id").toString() == "reviewed";
    QVERIFY(!hasManualReviewedAction);
    QCOMPARE(variant(review->property("bindings")).toMap().value("hide").toStringList(), (QStringList{"Del", "Backspace"}));
    QVariant result;
    QVERIFY(QMetaObject::invokeMethod(review, "assign", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, "next"), Q_ARG(QVariant, 0), Q_ARG(QVariant, "Ctrl+J")));
    QVERIFY(result.toString().isEmpty());
    QVERIFY(QMetaObject::invokeMethod(review, "assign", Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, "previous"), Q_ARG(QVariant, 0), Q_ARG(QVariant, "Ctrl+J")));
    QVERIFY(!result.toString().isEmpty());
    QCOMPARE(pfui::AppInfo::instance()->keySequence(Qt::Key_Delete, Qt::NoModifier), QString("Del"));
    QCOMPARE(pfui::AppInfo::instance()->keySequence(Qt::Key_D, Qt::ControlModifier), QString("Ctrl+D"));
    const auto prefs = pfui::AppInfo::instance()->loadPreferences();
    QCOMPARE(prefs.value("review").toMap().value("bindings").toMap().value("next").toStringList(), (QStringList{"Ctrl+J"}));
    auto* settings = engine.rootObjects().constFirst()->findChild<QObject*>("settingsDialog");
    QVERIFY(settings); QVERIFY(QMetaObject::invokeMethod(settings, "open"));
    auto* tabs = settings->findChild<QObject*>("settingsTabs"); QVERIFY(tabs); QCOMPARE(tabs->property("count").toInt(), 4);
    tabs->setProperty("currentIndex", 3);
    QVERIFY(settings->findChild<QObject*>("settingsHotkeysFlick"));
    QTest::qWait(100);
    auto* mainWindow = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst()); QVERIFY(mainWindow);
    const auto findKey = [&](const QString& name) {
        std::function<QQuickItem*(QQuickItem*)> visit = [&](QQuickItem* item) -> QQuickItem* {
            if (item->objectName() == name) return item;
            for (auto* child : item->childItems()) if (auto* result = visit(child)) return result;
            return nullptr;
        };
        return visit(mainWindow->contentItem());
    };
    auto* primary = findKey("hotkey_next_0"), *secondary = findKey("hotkey_favorite_1"); QVERIFY(primary && secondary);
    QCOMPARE(primary->height(), 28.0); QCOMPARE(secondary->height(), 28.0);
    auto* primaryFavorite = findKey("hotkey_favorite_0"); QVERIFY(primaryFavorite);
    const auto primaryPosition = primary->mapToItem(mainWindow->contentItem(), QPointF());
    const auto favoritePosition = primaryFavorite->mapToItem(mainWindow->contentItem(), QPointF());
    const auto secondaryPosition = secondary->mapToItem(mainWindow->contentItem(), QPointF());
    QVERIFY(qAbs(primaryPosition.x() - favoritePosition.x()) < 1.0);
    QVERIFY(secondaryPosition.x() >= favoritePosition.x() + primaryFavorite->width());
    QVERIFY(secondaryPosition.x() + secondary->width() <= mainWindow->width());

    QVERIFY(QMetaObject::invokeMethod(secondary, "clicked"));
    QTest::keyClick(mainWindow, Qt::Key_F, Qt::ControlModifier);
    QTRY_COMPARE(variant(review->property("bindings")).toMap().value("favorite").toStringList(), (QStringList{"F", "Ctrl+F"}));
    auto* clear = findKey("clear_hotkey_favorite_1"); QVERIFY(clear);
    QVERIFY(QMetaObject::invokeMethod(clear, "clicked"));
    QTRY_COMPARE(variant(review->property("bindings")).toMap().value("favorite").toStringList(), (QStringList{"F", ""}));
    QQmlComponent component(&engine);
    component.setData(R"(import QtQuick
import PfUi
ResultsRail { width: 300; height: 600; results: [{id:0, similarity:0.9}, {id:1, similarity:0.8}, {id:2, similarity:0.7}, {id:3, similarity:0.6}, {id:4, similarity:0.5}]
onExportSelectionChanged: function(rows) { selectedRows = rows }
onResultSelected: function(id) { selectedIndex = id }
})", QUrl());
    std::unique_ptr<QObject> rail(component.create()); QVERIFY2(rail != nullptr, qPrintable(component.errorString()));
    QVERIFY(QMetaObject::invokeMethod(review, "setMulti", Q_ARG(QVariant, true)));
    const auto select = [&](int id, int modifiers) { return QMetaObject::invokeMethod(rail.get(), "selectCard", Q_ARG(QVariant, id), Q_ARG(QVariant, modifiers)); };
    QVERIFY(select(0, 0)); QVERIFY(select(2, Qt::ControlModifier));
    QCOMPARE(variant(rail->property("selectedRows")).toMap().size(), 2);
    QVERIFY(select(4, Qt::ShiftModifier));
    const auto selected = variant(rail->property("selectedRows")).toMap();
    QCOMPARE(selected.size(), 3); QVERIFY(selected.contains("2") && selected.contains("3") && selected.contains("4"));
    QVERIFY(select(3, Qt::ControlModifier)); QCOMPARE(variant(rail->property("selectedRows")).toMap().size(), 2);
    rail->setProperty("results", QVariantList{QVariantMap{{"id",0},{"favorite",true}}, QVariantMap{{"id",1},{"hidden",true}}, QVariantMap{{"id",2}}});
    QCOMPARE(variant(rail->property("visibleResults")).toList().size(), 2);
    rail->setProperty("reviewFilter", "favorites"); QCOMPARE(variant(rail->property("visibleResults")).toList().size(), 1);
    rail->setProperty("reviewFilter", "hidden"); QCOMPARE(variant(rail->property("visibleResults")).toList().size(), 1);
    const auto capture = qEnvironmentVariable("PF_UI_CAPTURE_DIR");
    if (!capture.isEmpty()) { QDir().mkpath(capture); QTest::qWait(450); QVERIFY(qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst())->grabWindow().save(capture + "/hotkeys.png")); }
    QVERIFY(QMetaObject::invokeMethod(review, "reset")); QVERIFY(QMetaObject::invokeMethod(review, "setMulti", Q_ARG(QVariant, false)));
}

void UiSmokeTests::realWalkingClassificationTrimAndOrderedExport()
{
    const auto genv=qEnvironmentVariable("PF_ACTION_PIPELINE_GENV"),s03=qEnvironmentVariable("PF_ACTION_PIPELINE_S03");
    if(genv.isEmpty() || s03.isEmpty())QSKIP("Opt-in multi-source walking classification, trim and export");
    auto* analysis=pfui::AnalysisController::instance();
    analysis->setModelPath(qEnvironmentVariable("PF_MODEL_PATH"));analysis->setProviderChoice("cuda");
    analysis->setCachePath(qEnvironmentVariable("PF_ACTION_PIPELINE_CACHE"));analysis->setCacheLimitGb(64);
    analysis->setQualityProfile("fast");analysis->setAnalysisMode("combined");
    analysis->setSceneThreshold(27);analysis->setAccuracyPreset("fast");analysis->setExpandedSearch(true);
    // Match the recorded application audit policy explicitly. The preset name
    // alone does not apply QML's parameter changes in this C++ integration test.
    analysis->setSimilarityThreshold(.70);analysis->setCandidateThreshold(.48);
    analysis->setRepeatGap(8);analysis->setSameFileGap(3);analysis->setCrossFileGap(0);
    analysis->setDuplicateWindow(2);analysis->setNoiseFactor(1.25);analysis->setTimeWeight(.1);
    analysis->setNormalizeSize(true);analysis->setMirrorPoses(true);
    analysis->setMaxUniqueResults(500);analysis->analyzeFiles({genv,s03});
    QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy(),600000);
    int walk=-1,pose=-1;
    for(const auto& value:analysis->results()) {
        const auto row=value.toMap();
        if(row.value("leftStart").toDouble()>5 && row.value("leftStart").toDouble()<8
            && row.value("rightStart").toDouble()>1651 && row.value("rightStart").toDouble()<1655)
            walk=row.value("id").toInt();
        if(pose<0 && row.value("classification").toString().section('/',0,0)=="arms_extended")pose=row.value("id").toInt();
    }
    QVERIFY(walk>=0);QVERIFY(pose>=0);
    const auto original=analysis->results()[walk].toMap();
    QCOMPARE(original.value("classification").toString().section('/',0,0),QString("step"));
    const double start=original.value("leftClipStart").toDouble()+.1;
    QVERIFY(analysis->setResultRange(walk,start,original.value("leftClipEnd").toDouble(),
        original.value("rightClipStart").toDouble(),original.value("rightClipEnd").toDouble()));
    QTRY_VERIFY_WITH_TIMEOUT(!analysis->reviewBusy(),30000);
    QCOMPARE(analysis->results()[walk].toMap().value("classification").toString().section('/',0,0),QString("step"));
    QVERIFY(!analysis->results()[walk].toMap().value("classificationManual").toBool());
    QQmlApplicationEngine engine;pfui::AppInfo::registerQmlTypes();
    engine.load(QUrl("qrc:/qt/qml/PfUi/qml/Main.qml"));QVERIFY(!engine.rootObjects().isEmpty());
    auto* rail=engine.rootObjects().constFirst()->findChild<QObject*>("resultsRail");QVERIFY(rail);
    const auto variant=[](const QVariant& value) {return value.metaType()==QMetaType::fromType<QJSValue>() ? value.value<QJSValue>().toVariant():value;};
    QTRY_VERIFY(variant(rail->property("visibleResults")).toList().size()==analysis->results().size());
    const auto orderValue=variant(rail->property("classKeys")).toList();QStringList order;
    for(const auto& value:orderValue)order.push_back(value.toString());
    QVERIFY(order.indexOf("arms_extended")>=0);QVERIFY(order.indexOf("step")>order.indexOf("arms_extended"));
    int previous=-1;
    for(const auto& value:variant(rail->property("visibleResults")).toList()) {
        const auto row=value.toMap();const int rank=order.indexOf(row.value("classification").toString().section('/',0,0));
        QVERIFY(rank>=previous);previous=rank;
    }
    QTemporaryDir output;QVERIFY(output.isValid());QSignalSpy finished(analysis,&pfui::AnalysisController::exportFinished);
    QVERIFY(analysis->exportResults("FFMPEG",1,2,output.path(),"classified",{walk,pose},false,order,14,0,true));
    QTRY_VERIFY_WITH_TIMEOUT(!analysis->exportBusy(),60000);
    QCOMPARE(finished.size(),1);QVERIFY2(finished[0][0].toBool(),qPrintable(finished[0][1].toString()));
    const auto folders=QDir(output.path()).entryList(QDir::Dirs|QDir::NoDotAndDotDot);QCOMPARE(folders.size(),2);
    QString stepFolder,poseFolder;
    for(const auto& folder:folders) {
        if(folder.endsWith("_step"))stepFolder=folder;
        if(folder.endsWith("_arms_extended"))poseFolder=folder;
    }
    QVERIFY(!stepFolder.isEmpty());QVERIFY(!poseFolder.isEmpty());QVERIFY(poseFolder<stepFolder);
    const auto steps=QDir(output.path()+"/"+stepFolder).entryList({"*.mov"},QDir::Files);
    QCOMPARE(steps.size(),2);QVERIFY(steps[0].contains("0002_step"));
    for(const auto& clip:steps) {
        const QString side=clip.endsWith("_A.mov")?"left":"right";
        const auto row=analysis->results()[walk].toMap();
        pfcore::VideoDecoder sourceDecoder,exportDecoder;
        sourceDecoder.open(row.value(side+"Source").toString().toStdString());
        exportDecoder.open((output.path()+"/"+stepFolder+"/"+clip).toStdString());
        sourceDecoder.seek(row.value(side+"ClipStart").toDouble());
        pfcore::DecodedFrame sourceFrame,exportFrame;bool available=sourceDecoder.readNext(sourceFrame);int frames=0;
        while(available && sourceFrame.timestampSeconds<row.value(side+"ClipStart").toDouble()-1e-6)
            available=sourceDecoder.readNext(sourceFrame);
        while(available && sourceFrame.timestampSeconds<row.value(side+"ClipEnd").toDouble()-1e-6) {
            QVERIFY(exportDecoder.readNext(exportFrame));QVERIFY(sourceFrame.rgba==exportFrame.rgba);++frames;
            available=sourceDecoder.readNext(sourceFrame);
        }
        QVERIFY(frames>5);QVERIFY(!exportDecoder.readNext(exportFrame));
    }
}

void UiSmokeTests::realAutomaticClassConfirmationSurvivesTrim()
{
    const auto source=qEnvironmentVariable("PF_REAL_CLASSIFICATION_SOURCE");
    if(source.isEmpty()) QSKIP("Opt-in real-source manual classification and preview integration");
    QVERIFY(QFileInfo::exists(source));
    auto* analysis=pfui::AnalysisController::instance();
    analysis->setModelPath(qEnvironmentVariable("PF_MODEL_PATH"));
    analysis->setProviderChoice("cuda");
    analysis->setQualityProfile("fast");
    analysis->setAnalysisMode("combined");
    analysis->setAccuracyPreset("fast");
    analysis->setExpandedSearch(false);
    analysis->analyzeFiles({source});
    QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy(),180000);
    QVERIFY(!analysis->results().isEmpty());
    int id=-1;
    for(const auto& item:analysis->results()) {
        const auto row=item.toMap();
        const auto type=row.value("classification").toString().section('/',0,0);
        if(!type.contains('+') && !row.value("classificationManual").toBool()
            && row.value("leftClipEnd").toDouble()-row.value("leftClipStart").toDouble()>.5) {
            id=row.value("id").toInt();break;
        }
    }
    QVERIFY2(id>=0,"Fixture must contain a trimmable automatically classified single-action result");
    const auto before=analysis->results();
    const auto original=before[id].toMap();
    const auto type=original.value("classification").toString().section('/',0,0);
    analysis->setResultClassification({id},type);
    QVERIFY(analysis->results()[id].toMap().value("classificationManual").toBool());
    QCOMPARE(analysis->undoResultEdit().size(),1);
    QCOMPARE(analysis->results(),before);
    analysis->setResultClassification({id},type);
    const auto confirmed=analysis->results();
    const double start=original.value("leftClipStart").toDouble()+.1;
    QVERIFY(analysis->setResultRange(id,start,original.value("leftClipEnd").toDouble(),
        original.value("rightClipStart").toDouble(),original.value("rightClipEnd").toDouble()));
    QTRY_VERIFY_WITH_TIMEOUT(!analysis->reviewBusy(),20000);
    const auto trimmed=analysis->results()[id].toMap();
    QCOMPARE(trimmed.value("classification").toString(),type);
    QVERIFY(trimmed.value("classificationManual").toBool());
    QCOMPARE(trimmed.value("leftClipStart").toDouble(),start);
    QVERIFY(QFileInfo::exists(QUrl(trimmed.value("leftPreview").toString()).toLocalFile()));
    if(qEnvironmentVariable("PF_REAL_CLASSIFICATION_EXPORT")=="1") {
        QTemporaryDir output;QVERIFY(output.isValid());
        QSignalSpy finished(analysis,&pfui::AnalysisController::exportFinished);
        QVERIFY(analysis->exportResults("FFMPEG",0,2,output.path(),"classified",{id},false,{},14,0));
        QTRY_VERIFY_WITH_TIMEOUT(!analysis->exportBusy(),60000);
        QCOMPARE(finished.size(),1);
        QVERIFY2(finished[0][0].toBool(),qPrintable(finished[0][1].toString()));
        const auto clips=QDir(output.path()).entryList({"*.mov"},QDir::Files);
        QCOMPARE(clips.size(),2);
        for(int side=0;side<2;++side) {
            const QString key=side==0?"left":"right";
            const auto suffix=side==0?"_A.mov":"_B.mov";
            QString clip;
            for(const auto& name:clips)if(name.endsWith(suffix))clip=name;
            QVERIFY(!clip.isEmpty());
            const double begin=trimmed.value(key+"ClipStart").toDouble();
            const double end=trimmed.value(key+"ClipEnd").toDouble();
            pfcore::VideoDecoder originalDecoder,exportedDecoder;
            originalDecoder.open(trimmed.value(key+"Source").toString().toStdString());
            exportedDecoder.open((output.path()+"/"+clip).toStdString());
            QCOMPARE(exportedDecoder.info().width,originalDecoder.info().width);
            QCOMPARE(exportedDecoder.info().height,originalDecoder.info().height);
            originalDecoder.seek(begin);
            pfcore::DecodedFrame originalFrame,exportedFrame;
            bool available=originalDecoder.readNext(originalFrame);
            while(available && originalFrame.timestampSeconds<begin-1e-6)
                available=originalDecoder.readNext(originalFrame);
            int count=0;
            while(available && originalFrame.timestampSeconds<end-1e-6) {
                QVERIFY2(exportedDecoder.readNext(exportedFrame),"Export is missing a supported source frame");
                QVERIFY2(originalFrame.rgba==exportedFrame.rgba,"Exact export differs from the source's decoded pixels");
                ++count;available=originalDecoder.readNext(originalFrame);
            }
            QVERIFY(count>0);
            QVERIFY2(!exportedDecoder.readNext(exportedFrame),"Export includes frames after the selected interval");
        }
    }
    QCOMPARE(analysis->undoResultEdit().size(),1);
    QCOMPARE(analysis->results(),confirmed);
    QCOMPARE(analysis->undoResultEdit().size(),1);
    QCOMPARE(analysis->results(),before);
}

void UiSmokeTests::realReviewAdjustAndFindings()
{
    if (qEnvironmentVariableIsEmpty("PF_REAL_SOLDIER_SOURCE")) QSKIP("Opt-in real-video review/export integration");
    auto* analysis = pfui::AnalysisController::instance();
    if (analysis->results().size() < 2) {
        analysis->setModelPath(qEnvironmentVariable("PF_MODEL_PATH"));
        analysis->setProviderChoice("cuda"); analysis->setQualityProfile("fast");
        analysis->setAnalysisMode("combined"); analysis->setAccuracyPreset("fast");
        analysis->setExpandedSearch(false); analysis->analyzeFiles({qEnvironmentVariable("PF_REAL_SOLDIER_SOURCE")});
        QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy(), 300000);
    }
    QVERIFY(analysis->results().size() >= 2);
    pfui::AppInfo::registerQmlTypes(); QQmlApplicationEngine engine;
    engine.load(QUrl("qrc:/qt/qml/PfUi/qml/Main.qml")); QVERIFY(!engine.rootObjects().isEmpty());
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst()); QVERIFY(window);
    window->show(); window->requestActivate(); QTest::qWait(450);
    const int first = window->property("selectedResultIndex").toInt(); QVERIFY(first >= 0);
    const auto beforeBulk = analysis->results();
    const int other = first == 0 ? 1 : 0;
    QString replacementClass;
    for (const auto& type : {QString("raise"), QString("step"), QString("pull")}) {
        if (beforeBulk[first].toMap().value("classification").toString().section('/',0,0) != type
            && beforeBulk[other].toMap().value("classification").toString().section('/',0,0) != type) {
            replacementClass = type; break;
        }
    }
    analysis->setResultClassification({first,other}, replacementClass);
    // Explicitly choosing an existing automatic label must freeze the class,
    // not silently remain automatic. Undo restores the original scope.
    QCOMPARE(analysis->undoResultEdit().size(),2); QCOMPARE(analysis->results(),beforeBulk);
    bool checkedAutomaticConfirmation=false;
    for(const auto& item:beforeBulk) {
        const auto candidate=item.toMap();const auto type=candidate.value("classification").toString().section('/',0,0);
        if(type.contains('+') || candidate.value("classificationManual").toBool())continue;
        const auto id=candidate.value("id").toInt();
        analysis->setResultClassification({id},type);
        QVERIFY(analysis->results()[id].toMap().value("classificationManual").toBool());
        QCOMPARE(analysis->undoResultEdit().size(),1);QCOMPARE(analysis->results(),beforeBulk);
        checkedAutomaticConfirmation=true;
        break;
    }
    QVERIFY2(checkedAutomaticConfirmation,"Real review fixture needs an automatic single-action class");
    analysis->setResultClassification({first,other}, replacementClass);
    QCOMPARE(analysis->undoResultEdit().size(),2); QCOMPARE(analysis->results(),beforeBulk);
    analysis->setResultReview({first,other}, "reviewed",true);
    QVERIFY(analysis->results()[first].toMap().value("reviewed").toBool());
    QCOMPARE(analysis->undoResultEdit().size(),2); QCOMPARE(analysis->results(),beforeBulk);
    QVariantList allIds; for (const auto& item : beforeBulk) allIds.push_back(item.toMap().value("id"));
    analysis->setResultReview(allIds,"hidden",true);
    QTRY_COMPARE(window->property("selectedResultIndex").toInt(),-1);
    QTest::keyClick(window,Qt::Key_Z,Qt::ControlModifier);
    QTRY_COMPARE(analysis->results(),beforeBulk); QTRY_VERIFY(window->property("selectedResultIndex").toInt() >= 0);
    QVERIFY(QMetaObject::invokeMethod(window,"selectResult",Q_ARG(QVariant,first)));
    QTest::keyClick(window, Qt::Key_Delete);
    QTRY_VERIFY(analysis->results()[first].toMap().value("hidden").toBool());
    QTRY_VERIFY(window->property("selectedResultIndex").toInt() != first);
    auto* rail = window->findChild<QObject*>("resultsRail"); QVERIFY(rail);
    rail->setProperty("reviewFilter", "hidden"); QTRY_COMPARE(window->property("selectedResultIndex").toInt(), first);
    QTest::keyClick(window, Qt::Key_Backspace); QTRY_VERIFY(!analysis->results()[first].toMap().value("hidden").toBool());
    rail->setProperty("reviewFilter", "all");
    QVERIFY(QMetaObject::invokeMethod(window, "selectResult", Q_ARG(QVariant, first)));
    QTest::keyClick(window, Qt::Key_F); QTRY_VERIFY(analysis->results()[first].toMap().value("favorite").toBool());
    QTest::qWait(500);
    const auto findVisual = [](QQuickItem* root, const QString& name) {
        std::function<QQuickItem*(QQuickItem*)> visit = [&](QQuickItem* item) -> QQuickItem* {
            if (item->objectName() == name) return item;
            for (auto* child : item->childItems()) if (auto* found = visit(child)) return found;
            return nullptr;
        };
        return visit(root);
    };
    // Exercise both deletion paths from the middle, where a reset to row one is visible.
    const auto visibleBefore = rail->property("visibleResults").value<QJSValue>().toVariant().toList();
    if (visibleBefore.size() >= 4) {
        const int middle = visibleBefore[1].toMap().value("id").toInt();
        const int neighbor = visibleBefore[2].toMap().value("id").toInt();
        QVERIFY(QMetaObject::invokeMethod(window, "selectResult", Q_ARG(QVariant, middle)));
        QTest::keyClick(window, Qt::Key_Backspace);
        QTRY_COMPARE(window->property("selectedResultIndex").toInt(), neighbor);
        analysis->setResultReview({middle}, "hidden", false);
        QVERIFY(QMetaObject::invokeMethod(window, "selectResult", Q_ARG(QVariant, middle)));
        QTest::qWait(250);
        auto* remove = findVisual(window->contentItem(), "removePairButton" + QString::number(middle)); QVERIFY(remove);
        QVERIFY(QMetaObject::invokeMethod(remove, "clicked"));
        QTRY_COMPARE(window->property("selectedResultIndex").toInt(), neighbor);
        analysis->setResultReview({middle}, "hidden", false);
    }
    QVERIFY(QMetaObject::invokeMethod(window, "selectResult", Q_ARG(QVariant, first)));
    QTest::qWait(250);
    auto* card = findVisual(window->contentItem(), "resultCard" + QString::number(first)); QVERIFY(card);
    QVERIFY(!findVisual(window->contentItem(), "reviewedPairButton" + QString::number(first)));
    const bool wasReviewed = analysis->results()[first].toMap().value("reviewed").toBool();
    if (wasReviewed) {
        analysis->setResultReview({first}, "reviewed", false);
        QTRY_VERIFY_WITH_TIMEOUT(!analysis->results()[first].toMap().value("reviewed").toBool(), 5000);
    }
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier,
        card->mapToScene(QPointF(card->width() * .5, card->height() * .5)).toPoint());
    QTRY_VERIFY_WITH_TIMEOUT(analysis->results()[first].toMap().value("reviewed").toBool(), 5000);
    QVERIFY(!analysis->undoResultEdit().isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(!analysis->results()[first].toMap().value("reviewed").toBool(), 5000);
    if (wasReviewed) analysis->setResultReview({first}, "reviewed", true);
    card = findVisual(window->contentItem(), "resultCard" + QString::number(first)); QVERIFY(card);
    QCOMPARE(card->z(), 2.0);
    const auto variantRows = rail->property("visibleResults").value<QJSValue>().toVariant().toList();
    if (variantRows.size() > 1) {
        const auto nextId = variantRows[1].toMap().value("id").toInt();
        auto* next = findVisual(window->contentItem(), "resultCard" + QString::number(nextId)); QVERIFY(next);
        QVERIFY2(next->y() >= card->y() + card->height() + 4, qPrintable(QString("Card overlap: first y=%1 h=%2, next y=%3").arg(card->y()).arg(card->height()).arg(next->y())));
    }
    const auto row = analysis->results()[first].toMap();
    const double start = row.value("leftClipStart").toDouble(), end = row.value("leftClipEnd").toDouble();
    const double rightStart = row.value("rightClipStart").toDouble(), rightEnd = row.value("rightClipEnd").toDouble();
    QVERIFY(!analysis->setResultRange(first, -1, end, rightStart, rightEnd));
    QVERIFY(analysis->setResultRange(first, start + 0.04, end, rightStart, rightEnd));
    const auto reviewSnapshot = analysis->results();
    analysis->inspectFiles({}); analysis->analyzeFiles({});
    QCOMPARE(analysis->results(), reviewSnapshot);
    QTRY_VERIFY_WITH_TIMEOUT(!analysis->reviewBusy(), 20000);
    QCOMPARE(analysis->results()[first].toMap().value("leftClipStart").toDouble(), start + 0.04);
    QVERIFY(!analysis->undoResultEdit().isEmpty());
    QCOMPARE(analysis->results()[first].toMap().value("leftClipStart").toDouble(),start);
    QVERIFY(analysis->setResultRange(first,start + 0.04,end,rightStart,rightEnd));
    QTRY_VERIFY_WITH_TIMEOUT(!analysis->reviewBusy(),20000);
    QVERIFY(QFileInfo::exists(QUrl(analysis->results()[first].toMap().value("leftPreview").toString()).toLocalFile()));
    QTemporaryDir output; QVERIFY(output.isValid()); QSignalSpy finished(analysis, &pfui::AnalysisController::exportFinished);
    QVERIFY(analysis->exportFindings(output.path(), QVariantList{first, first, 1, -1}, 3));
    const auto exportSnapshot = analysis->results();
    analysis->inspectFiles({}); analysis->analyzeFiles({});
    QCOMPARE(analysis->results(), exportSnapshot);
    QTRY_VERIFY_WITH_TIMEOUT(!analysis->exportBusy(), 45000);
    QCOMPARE(finished.size(), 1); QVERIFY2(finished[0][0].toBool(), qPrintable(finished[0][1].toString()));
    const auto files = QDir(output.path()).entryList({"*.png"}, QDir::Files); QCOMPARE(files.size(), 1);
    const QImage image(output.path() + '/' + files[0]); QVERIFY(!image.isNull()); QCOMPARE(image.width(), 1920); QVERIFY(image.height() >= 744);
    const auto capture = qEnvironmentVariable("PF_UI_CAPTURE_DIR");
    if (!capture.isEmpty()) {
        QDir().mkpath(capture); QFile::remove(capture + "/findings.png"); QVERIFY(QFile::copy(output.path() + '/' + files[0], capture + "/findings.png"));
        QVERIFY(window->grabWindow().save(capture + "/results.png"));
        auto* adjust = window->findChild<QObject*>("pairAdjustDialog"); QVERIFY(adjust); adjust->setProperty("record", analysis->results()[first]);
        QVERIFY(QMetaObject::invokeMethod(adjust, "open")); QTest::qWait(450); QVERIFY(window->grabWindow().save(capture + "/adjust.png"));
        QVERIFY(QMetaObject::invokeMethod(adjust, "close"));
    }
    auto* adjust = window->findChild<QObject*>("pairAdjustDialog"); QVERIFY(adjust);
    adjust->setProperty("record", analysis->results()[first]);
    QVERIFY(QMetaObject::invokeMethod(adjust, "open"));
    QTRY_VERIFY_WITH_TIMEOUT(!analysis->reviewBusy() && !adjust->property("previewLeft").toString().isEmpty(), 20000);
    const double draftStart = adjust->property("leftStart").toDouble();
    QSignalSpy previews(analysis, &pfui::AnalysisController::reviewPreviewsReady);
    QVERIFY(QMetaObject::invokeMethod(adjust, "step", Q_ARG(QVariant, "left"), Q_ARG(QVariant, 1)));
    QTRY_VERIFY_WITH_TIMEOUT(previews.size() > 0 && !analysis->reviewBusy(), 20000);
    QVERIFY(adjust->property("leftStart").toDouble() > draftStart);
    QVERIFY(!adjust->property("previewLeft").toString().isEmpty());
    QCOMPARE(analysis->results()[first].toMap().value("leftClipStart").toDouble(), start + 0.04);
    for (const auto* side : {"adjustLeftView", "adjustRightView"}) {
        auto* panel = adjust->findChild<QObject*>(side); QVERIFY(panel);
        auto* image = panel->findChild<QObject*>("comparisonFrameImage"); QVERIFY(image);
        QTRY_COMPARE_WITH_TIMEOUT(image->property("status").toInt(), 1, 10000);
    }
    QTest::qWait(250);
    if (!capture.isEmpty()) QVERIFY(window->grabWindow().save(capture + "/adjust-timeline.png"));
    QVERIFY(QMetaObject::invokeMethod(adjust, "close")); QTest::qWait(300);
    auto* exportDialog = window->findChild<QObject*>("exportDialog"); QVERIFY(exportDialog);
    exportDialog->setProperty("selectedRows", QVariantMap{{QString::number(first), true}});
    QVERIFY(QMetaObject::invokeMethod(exportDialog, "open")); QTest::qWait(350);
    auto* quality = exportDialog->findChild<QObject*>("exportQualityMode"); QVERIFY(quality);
    QCOMPARE(quality->property("currentIndex").toInt(), 1);
    QCOMPARE(exportDialog->property("encodingQuality").toInt(), 18);
    quality->setProperty("currentIndex", 3);
    QVERIFY(QMetaObject::invokeMethod(quality, "activated", Q_ARG(int, 3)));
    auto* bitrate = exportDialog->findChild<QObject*>("exportBitrateField"); QVERIFY(bitrate);
    bitrate->setProperty("text", "40"); QCOMPARE(exportDialog->property("videoBitrateKbps").toInt(), 40000);
    bitrate->setProperty("text", "0"); QVERIFY(!exportDialog->property("qualityValid").toBool());
    bitrate->setProperty("text", "40"); QVERIFY(exportDialog->property("qualityValid").toBool());
    if (!capture.isEmpty()) QVERIFY(window->grabWindow().save(capture + "/export-quality.png"));
    QVERIFY(QMetaObject::invokeMethod(exportDialog, "close")); QTest::qWait(300);
    QVERIFY(analysis->setResultRange(first, start + 0.04, start + 7.54, rightStart, rightEnd));
    QTRY_VERIFY_WITH_TIMEOUT(!analysis->reviewBusy(), 20000);
    QTemporaryDir clips; finished.clear();
    QVERIFY(analysis->exportResults("FFMPEG", 0, 0, clips.path(), "review", QVariantList{first}, false, {}, 14, 0));
    QTRY_VERIFY_WITH_TIMEOUT(!analysis->exportBusy(), 45000);
    QCOMPARE(finished.size(), 1); QVERIFY2(finished[0][0].toBool(), qPrintable(finished[0][1].toString()));
    const auto mp4s = QDir(clips.path()).entryList({"*.mp4"}, QDir::Files); QCOMPARE(mp4s.size(), 2);
    pfcore::VideoDecoder decoded; decoded.open((clips.path() + '/' + mp4s[0]).toStdString());
    QVERIFY(std::abs(decoded.info().durationSeconds - 7.5) < 0.1);
    QVERIFY(analysis->resetResultRange(first)); QTRY_VERIFY_WITH_TIMEOUT(!analysis->reviewBusy(), 20000);
    QCOMPARE(analysis->results()[first].toMap().value("leftClipStart").toDouble(), start);
    const auto ownedPreview = QUrl(analysis->results()[first].toMap().value("leftPreview").toString()).toLocalFile();
    QVERIFY(QFileInfo::exists(ownedPreview));
    for (int i = 0; i < 20; ++i) analysis->setResultReview({first}, "reviewed", i % 2 == 0);
    int undone = 0; while (analysis->canUndoReview()) { QVERIFY(!analysis->undoResultEdit().isEmpty()); ++undone; }
    QCOMPARE(undone,16);
    analysis->inspectFiles({});
    QVERIFY(analysis->results().isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(!QFileInfo::exists(QFileInfo(ownedPreview).absolutePath()), 4000);
}

void UiSmokeTests::idleWindowAndDiscordMemoryPlateau()
{
#if defined(Q_OS_WIN)
    const int seconds = qEnvironmentVariableIntValue("PF_IDLE_MEMORY_SECONDS");
    if (seconds < 90) QSKIP("Opt-in long idle-memory test");
    pfui::AppInfo::registerQmlTypes();
    QQmlApplicationEngine engine;
    engine.load(QUrl("qrc:/qt/qml/PfUi/qml/Main.qml"));
    QVERIFY(!engine.rootObjects().isEmpty());
    auto* discord = engine.singletonInstance<pfui::DiscordPresence*>(qmlTypeId("PfUiBridge", 1, 0, "Discord"));
    discord->setActivity(false, false, 0, pfui::AnalysisController::instance()->matchCount(), {}, "ru");
    discord->start();
    QFile log(qEnvironmentVariable("PF_IDLE_MEMORY_LOG"));
    QVERIFY(log.open(QIODevice::WriteOnly | QIODevice::Text));
    qint64 settled = 0, maximumAfterSettling = 0;
    for (int elapsed = 0; elapsed <= seconds; elapsed += 2) {
        PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb = sizeof(memory);
        QVERIFY(K32GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)));
        const auto bytes = static_cast<qint64>(memory.PrivateUsage);
        log.write(QByteArray::number(elapsed) + "," + QByteArray::number(bytes) + "," + QByteArray::number(pfgpu::processSessionCache().stats().live) + "\n");
        log.flush();
        // First minute includes renderer warmup and deferred model teardown.
        if (elapsed == 90) settled = bytes;
        if (elapsed >= 90) maximumAfterSettling = std::max(maximumAfterSettling, bytes);
        QTest::qWait(2000);
    }
    discord->stop();
    QVERIFY2(maximumAfterSettling - settled < 64 * 1024 * 1024, "Idle private-memory growth exceeds 64 MiB");
    QCOMPARE(pfgpu::processSessionCache().stats().live, 0u);
    qInfo("PF_IDLE_MEMORY settled_bytes=%lld maximum_bytes=%lld", settled, maximumAfterSettling);
#else
    QSKIP("Windows process-private-memory audit");
#endif
}

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

void UiSmokeTests::manualSortAndComposeDialogs()
{
    const auto variant=[](const QVariant& value) { return value.metaType()==QMetaType::fromType<QJSValue>() ? value.value<QJSValue>().toVariant() : value; };
    pfui::AppInfo::registerQmlTypes();
    QQmlApplicationEngine engine; engine.load(QUrl("qrc:/qt/qml/PfUi/qml/Main.qml"));
    QVERIFY(!engine.rootObjects().isEmpty());
    auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().first()); QVERIFY(window);
    auto* manual=window->findChild<QObject*>("manualSortDialog"); QVERIFY(manual);
    auto* compose=window->findChild<QObject*>("pairComposeDialog"); QVERIFY(compose);
    QVariantList rows;
    for(int id=0;id<25;++id) rows.append(QVariantMap{{"id",id},{"classification","head_front"},{"manualGroup",id<4?"Моя группа":""},{"leftRemoved",id==3}});
    manual->setProperty("rows",rows); QVERIFY(QMetaObject::invokeMethod(manual,"open"));
    QTRY_COMPARE(manual->property("pageCount").toInt(),3);
    QVariant ignored;
    QVERIFY(QMetaObject::invokeMethod(manual,"pick",Q_RETURN_ARG(QVariant,ignored),Q_ARG(QVariant,0),Q_ARG(QVariant,0)));
    QVERIFY(QMetaObject::invokeMethod(manual,"pick",Q_RETURN_ARG(QVariant,ignored),Q_ARG(QVariant,2),Q_ARG(QVariant,int(Qt::ControlModifier))));
    QCOMPARE(variant(manual->property("selectedIds")).toList(),(QVariantList{0,2}));
    const auto findVisualItem = [](QQuickItem* parent, const QString& name, const auto& self) -> QQuickItem* {
        if (!parent) return nullptr;
        if (parent->objectName() == name) return parent;
        for (auto* child : parent->childItems()) if (auto* found = self(child, name, self)) return found;
        return nullptr;
    };
    QQuickItem* animatedCard=nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((animatedCard=findVisualItem(window->contentItem(),"manualSortCard0",findVisualItem))!=nullptr,1000);
    QTRY_VERIFY_WITH_TIMEOUT(qAbs(animatedCard->property("scale").toReal()-1.012)<.002,1000);
    auto* theme=engine.singletonInstance<QObject*>(qmlTypeId("PfUi",1,0,"Theme")); QVERIFY(theme);
    theme->setProperty("reducedMotion",true);
    QVERIFY(QMetaObject::invokeMethod(manual,"pick",Q_RETURN_ARG(QVariant,ignored),Q_ARG(QVariant,0),Q_ARG(QVariant,int(Qt::ControlModifier))));
    QCOMPARE(variant(manual->property("selectedIds")).toList(),(QVariantList{2}));
    QVERIFY(qAbs(animatedCard->property("scale").toReal()-1.0)<.001);
    theme->setProperty("reducedMotion",false);
    QVERIFY(QMetaObject::invokeMethod(manual,"pick",Q_RETURN_ARG(QVariant,ignored),Q_ARG(QVariant,14),Q_ARG(QVariant,int(Qt::ShiftModifier))));
    QCOMPARE(variant(manual->property("selectedIds")).toList().size(),15);
    QVERIFY(QMetaObject::invokeMethod(manual,"goToPage",Q_ARG(QVariant,1)));
    QCOMPARE(manual->property("page").toInt(),1);
    const auto capture=qEnvironmentVariable("PF_UI_CAPTURE_DIR");
    if(!capture.isEmpty()){QDir().mkpath(capture);QTest::qWait(400);QVERIFY(window->grabWindow().save(capture+"/manual-sort.png"));}
    QVERIFY(QMetaObject::invokeMethod(manual,"close")); QTest::qWait(250);
    compose->setProperty("record",rows[0]); QVERIFY(QMetaObject::invokeMethod(compose,"open"));
    compose->setProperty("candidates",QVariantList{rows[1]});
    auto* choice=window->findChild<QObject*>("composeDonorChoice"); QVERIFY(choice);choice->setProperty("currentIndex",0);
    QTRY_VERIFY(!variant(compose->property("donor")).toMap().isEmpty());
    if(!capture.isEmpty()){QTest::qWait(450);QVERIFY(window->grabWindow().save(capture+"/compose.png"));}
    auto* l10n=engine.singletonInstance<QObject*>(qmlTypeId("PfUi",1,0,"L10n")); QVERIFY(l10n);
    l10n->setProperty("language","ru");
    QVariant translated;
    const QString fastError = "Fast montage requires compatible video/audio stream parameters. Choose exact/lossless or re-encoding quality.";
    QVERIFY(QMetaObject::invokeMethod(l10n,"status",Q_RETURN_ARG(QVariant,translated),Q_ARG(QVariant,fastError)));
    QVERIFY(translated.toString().startsWith("Быстрая склейка копирует потоки"));
    const QString geometryError = "Lossless montage geometry mismatch: 1920x1080, SAR 1.000, rotation 0.0° vs 3840x2160, SAR 1.000, rotation 0.0°";
    QVERIFY(QMetaObject::invokeMethod(l10n,"status",Q_RETURN_ARG(QVariant,translated),Q_ARG(QVariant,geometryError)));
    QVERIFY(translated.toString().startsWith("Точная склейка без потери качества"));
    l10n->setProperty("language","en");
    QVERIFY(QMetaObject::invokeMethod(l10n,"status",Q_RETURN_ARG(QVariant,translated),Q_ARG(QVariant,geometryError)));
    QVERIFY(translated.toString().startsWith("Exact lossless montage requires matching video geometry"));
    auto* exportDialog=window->findChild<QObject*>("exportDialog"); QVERIFY(exportDialog);
    auto* exportStatus=exportDialog->findChild<QObject*>("exportStatusText"); QVERIFY(exportStatus);
    exportDialog->setProperty("exportStatus",geometryError);
    QCOMPARE(exportStatus->property("text").toString(),translated.toString());
    l10n->setProperty("language","ru");
    QVERIFY(QMetaObject::invokeMethod(l10n,"status",Q_RETURN_ARG(QVariant,translated),Q_ARG(QVariant,fastError)));
    exportDialog->setProperty("exportStatus",fastError);
    QCOMPARE(exportStatus->property("text").toString(),translated.toString());
    QVERIFY(QMetaObject::invokeMethod(compose,"close"));
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

void UiSmokeTests::languageTabOffersSixPersistentChoices()
{
    pfui::AppInfo::registerQmlTypes();
    const auto saved = pfui::AppInfo::instance()->loadPreferences();
    struct Restore { QVariantMap value; ~Restore() { pfui::AppInfo::instance()->savePreferences(value); } } restore{saved};
    QQmlApplicationEngine engine;
    auto* window = loadWindow(engine); QVERIFY(window);
    auto* popup = window->findChild<QObject*>("settingsDialog"); QVERIFY(popup);
    auto* tabs = popup->findChild<QObject*>("settingsTabs"); QVERIFY(tabs);
    QCOMPARE(tabs->property("count").toInt(), 4);
    auto* languageTab = popup->findChild<QObject*>("settingsLanguageTab"); QVERIFY(languageTab);
    tabs->setProperty("currentIndex", 2);
    QVERIFY(popup->findChild<QObject*>("settingsLanguagePage"));
    const QStringList ids{"ru", "en", "ky", "kk", "uk", "tr"};
    const QStringList names{"Русский", "English", "Кыргызча", "Қазақша", "Українська", "Türkçe"};
    QCOMPARE(popup->property("languageIds").toStringList(), ids);
    QCOMPARE(popup->property("languageNames").toStringList(), names);
    for (const auto& id : ids) {
        QVERIFY(QMetaObject::invokeMethod(popup, "setLanguage", Q_ARG(QVariant, id)));
        QCOMPARE(pfui::AppInfo::instance()->loadPreferences().value("language").toString(), id);
    }
}

void UiSmokeTests::rc23PresetsTranslationsAndFlagMotion()
{
    pfui::AppInfo::registerQmlTypes();
    const auto saved=pfui::AppInfo::instance()->loadPreferences();
    const auto restore=qScopeGuard([saved]{pfui::AppInfo::instance()->savePreferences(saved);});
    QQmlApplicationEngine engine;
    auto* window=loadWindow(engine);QVERIFY(window);
    auto* popup=window->findChild<QObject*>("settingsDialog");QVERIFY(popup);
    QVERIFY(QMetaObject::invokeMethod(popup,"open"));
    QTRY_VERIFY(popup->property("opened").toBool());
    auto* tabs=popup->findChild<QObject*>("settingsTabs");QVERIFY(tabs);
    QQmlComponent component(&engine);
    component.setData(R"(import QtQuick
import PfUi
QtObject {
    property bool gradient: Theme.accentGradientEnabled
    property color a: Theme.accent
    property color b: Theme.accentSecondary
    property color text: Theme.textPrimary
    property color accentText: Theme.accentText
    property color rail: Theme.rail
    property string add: L10n.t("sources.add")
    property string posture: L10n.classificationLabel("seated_lying")
    property string status: L10n.status("Матчер: Ищем похожие движения · 1 / 4")
    function light() { Theme.darkMode = false; Theme.reducedMotion = false }
    function dark() { Theme.darkMode = true }
    function setAccent(color) { Theme.accent = color }
})",QUrl());
    std::unique_ptr<QObject> probe(component.create());QVERIFY2(probe,qPrintable(component.errorString()));
    tabs->setProperty("currentIndex",1);
    auto* gradients=popup->findChild<QObject*>("appearanceGradientChoice");QVERIFY(gradients);
    for(int index=1;index<=3;++index) {
        gradients->setProperty("currentIndex",index);
        QVERIFY(QMetaObject::invokeMethod(gradients,"activated",Q_ARG(int,index)));
        QVERIFY(probe->property("gradient").toBool());
        QVERIFY(probe->property("a").value<QColor>()!=probe->property("b").value<QColor>());
        QCOMPARE(gradients->property("currentIndex").toInt(),index);
    }
    QVERIFY(QMetaObject::invokeMethod(probe.get(),"light"));
    const auto luminance=[](const QColor& color) {
        const auto linear=[](double v){return v<=.04045 ? v/12.92 : std::pow((v+.055)/1.055,2.4);};
        return .2126*linear(color.redF())+.7152*linear(color.greenF())+.0722*linear(color.blueF());
    };
    for(const auto mode:{"light","dark"}) {
        QVERIFY(QMetaObject::invokeMethod(probe.get(),mode));
        for(const auto color:{"#000000","#FFFFFF","#FFFF00","#5D8DDE"}) {
            QVERIFY(QMetaObject::invokeMethod(probe.get(),"setAccent",Q_ARG(QVariant,color)));
            const auto a=luminance(probe->property("accentText").value<QColor>());
            const auto b=luminance(probe->property("rail").value<QColor>());
            QVERIFY((std::max(a,b)+.05)/(std::min(a,b)+.05)>=4.5);
        }
    }
    QVERIFY(QMetaObject::invokeMethod(probe.get(),"light"));
    QVERIFY(QMetaObject::invokeMethod(popup,"applyAccent",Q_ARG(QVariant,"aurora")));
    pfui::AnalysisController::instance()->setProviderChoice("auto");
    auto* gpuLabel=window->findChild<QObject*>("gpuStatusLabel");QVERIFY(gpuLabel);
    QCOMPARE(gpuLabel->property("color").value<QColor>(),probe->property("text").value<QColor>());
    tabs->setProperty("currentIndex",2);
    const QStringList ids{"ru","en","ky","kk","uk","tr"};
    const QStringList labels{"Добавить видео","Add video","Видео кошуу","Бейне қосу","Додати відео","Video ekle"};
    for(int i=0;i<ids.size();++i) {
        QVERIFY(QMetaObject::invokeMethod(popup,"setLanguage",Q_ARG(QVariant,ids[i])));
        QCOMPARE(probe->property("add").toString(),labels[i]);
        QVERIFY(!probe->property("posture").toString().startsWith("class."));
        if(i>1)QVERIFY(!probe->property("status").toString().contains("Ищем"));
    }
    const auto findVisual=[](QQuickItem* item,const QString& name,const auto& self)->QQuickItem* {
        if(item->objectName()==name)return item;
        for(auto* child:item->childItems())if(auto* found=self(child,name,self))return found;
        return nullptr;
    };
    auto* languageButton=findVisual(window->contentItem(),"languageButton_0",findVisual);QVERIFY(languageButton);
    auto* flag=findVisual(window->contentItem(),"languageFlag_0",findVisual);QVERIFY(flag);
    QTest::qWait(300);
    const auto point=languageButton->mapToScene(QPointF(languageButton->width()/2,languageButton->height()/2)).toPoint();
    QTest::mouseMove(window,point);
    QTRY_VERIFY(languageButton->property("hovered").toBool());
    QTest::qWait(90);
    QVERIFY(flag->property("reveal").toDouble()>0);
    const auto capture=qEnvironmentVariable("PF_UI_CAPTURE_DIR");
    if(!capture.isEmpty()) {QDir().mkpath(capture);QVERIFY(window->grabWindow().save(capture+"/rc23-language-paint.png"));}
    QTRY_COMPARE(flag->property("reveal").toDouble(),1.0);
    // Capture each language card only after its reveal animation is complete.
    // The earlier mid-animation-only capture missed the final crop, clipping,
    // and opacity regressions that appeared in real use.
    if(!capture.isEmpty()) {
        QVERIFY(window->grabWindow().save(capture+"/rc23-language-paint-complete.png"));
        for(int i=0;i<ids.size();++i) {
            const auto buttonName=QStringLiteral("languageButton_%1").arg(i);
            const auto flagName=QStringLiteral("languageFlag_%1").arg(i);
            auto* button=findVisual(window->contentItem(),buttonName,findVisual);QVERIFY(button);
            auto* backdrop=findVisual(window->contentItem(),flagName,findVisual);QVERIFY(backdrop);
            const auto target=button->mapToScene(QPointF(button->width()/2,button->height()/2)).toPoint();
            QTest::mouseMove(window,target);
            QTRY_VERIFY(button->property("hovered").toBool());
            QTRY_VERIFY(backdrop->property("active").toBool());
            QTRY_VERIFY(backdrop->property("imageReady").toBool());
            QTRY_COMPARE(backdrop->property("reveal").toDouble(),1.0);
            QVERIFY(window->grabWindow().save(capture+QStringLiteral("/rc23-language-%1-complete.png").arg(ids[i])));
        }
    }
    QVERIFY(QMetaObject::invokeMethod(popup,"close"));
    QTRY_VERIFY(!popup->property("visible").toBool());
    if(!capture.isEmpty()) {QTest::qWait(200);QVERIFY(window->grabWindow().save(capture+"/rc23-light.png"));}
    auto* menu=window->findChild<QObject*>("providerMenu");QVERIFY(menu);
    QVERIFY(QMetaObject::invokeMethod(menu,"open"));
    QTRY_VERIFY(menu->property("opened").toBool());
    auto* automatic=findVisual(window->contentItem(),"providerName_auto",findVisual);QVERIFY(automatic);
    auto* cuda=findVisual(window->contentItem(),"providerName_cuda",findVisual);QVERIFY(cuda);
    QVERIFY(automatic->property("text").toString()!=cuda->property("text").toString());
    auto* cudaItem=findVisual(window->contentItem(),"providerItem_cuda",findVisual);QVERIFY(cudaItem);
    QCOMPARE(cudaItem->property("isReady").toBool(),pfui::AppInfo::instance()->backendAvailable("cuda"));
    if(!capture.isEmpty())QVERIFY(window->grabWindow().save(capture+"/rc23-provider-light.png"));
    QVERIFY(QMetaObject::invokeMethod(menu,"close"));
}

void UiSmokeTests::customAccentPaletteUpdatesImmediatelyAndPersists()
{
    pfui::AppInfo::registerQmlTypes();
    const auto saved = pfui::AppInfo::instance()->loadPreferences();
    struct Restore { QVariantMap value; ~Restore() { pfui::AppInfo::instance()->savePreferences(value); } } restore{saved};
    for (int pass = 0; pass < 2; ++pass) {
        QQmlApplicationEngine engine;
        auto* window = loadWindow(engine); QVERIFY(window);
        auto* popup = window->findChild<QObject*>("settingsDialog"); QVERIFY(popup);
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nimport PfUi\nItem { property color accent: Theme.accent; property color accentSecondary: Theme.accentSecondary; property color sage: Theme.sage; property bool gradientEnabled: Theme.accentGradientEnabled }",QUrl());
        std::unique_ptr<QObject> probe(component.create()); QVERIFY(probe);
        if (!pass) {
            QVERIFY(QMetaObject::invokeMethod(popup,"open"));
            QTRY_VERIFY(popup->property("opened").toBool());
            auto* tabs = popup->findChild<QObject*>("settingsTabs"); QVERIFY(tabs); tabs->setProperty("currentIndex",1);
            QTest::qWait(250);
            auto* accentChoice = popup->findChild<QObject*>("appearanceAccentChoice"); QVERIFY(accentChoice);
            accentChoice->setProperty("currentIndex", 4);
            QVERIFY(QMetaObject::invokeMethod(accentChoice, "activated", Q_ARG(int, 4)));
            auto* picker = popup->findChild<QObject*>("accentColorPicker"); QVERIFY(picker);
            QTRY_VERIFY(picker->property("opened").toBool());
            auto* hex = picker->findChild<QObject*>("accentPaletteHex"); QVERIFY(hex);
            hex->setProperty("text","#12ABCD");
            QVERIFY(QMetaObject::invokeMethod(hex,"textEdited"));
            QCOMPARE(probe->property("accent").value<QColor>().name(),QString("#12abcd"));
            QVERIFY(picker->property("opened").toBool());
            QCOMPARE(accentChoice->property("currentIndex").toInt(),4);
            QVERIFY(!probe->property("gradientEnabled").toBool());
            QCOMPARE(probe->property("sage").value<QColor>(), probe->property("accent").value<QColor>());
            QVariant accepted;
            QVERIFY(QMetaObject::invokeMethod(popup,"applyCustomAccent",Q_RETURN_ARG(QVariant,accepted),Q_ARG(QVariant,"invalid")));
            QVERIFY(!accepted.toBool()); QCOMPARE(probe->property("accent").value<QColor>().name(),QString("#12abcd"));
            auto* gradientChoice = popup->findChild<QObject*>("appearanceGradientChoice"); QVERIFY(gradientChoice);
            gradientChoice->setProperty("currentIndex", 4);
            QVERIFY(QMetaObject::invokeMethod(gradientChoice, "activated", Q_ARG(int, 4)));
            QVERIFY(probe->property("gradientEnabled").toBool());
            QVERIFY(QMetaObject::invokeMethod(popup, "selectAccentTarget", Q_ARG(QVariant, "end")));
            QVERIFY(QMetaObject::invokeMethod(popup,"applyCustomAccent",Q_RETURN_ARG(QVariant,accepted),Q_ARG(QVariant,"#AABBCC")));
            QVERIFY(accepted.toBool());
            QCOMPARE(probe->property("accentSecondary").value<QColor>().name(), QString("#aabbcc"));
            QCOMPARE(probe->property("sage").value<QColor>(), probe->property("accentSecondary").value<QColor>());
            const auto capture = qEnvironmentVariable("PF_UI_CAPTURE_DIR");
            if (!capture.isEmpty()) { QDir().mkpath(capture); QTest::qWait(250); QVERIFY(window->grabWindow().save(capture+"/accent-palette.png")); }
            QVERIFY(QMetaObject::invokeMethod(picker,"close")); QVERIFY(QMetaObject::invokeMethod(popup,"close"));
        } else {
            QCOMPARE(probe->property("accent").value<QColor>().name(),QString("#12abcd"));
            QCOMPARE(probe->property("accentSecondary").value<QColor>().name(),QString("#aabbcc"));
            QVERIFY(probe->property("gradientEnabled").toBool());
            QCOMPARE(probe->property("sage").value<QColor>(), probe->property("accentSecondary").value<QColor>());
            QCOMPARE(popup->findChild<QObject*>("appearanceGradientChoice")->property("currentIndex").toInt(),4);
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
    auto* add=sources->findChild<QQuickItem*>("addSourcesButton");
    QVERIFY(list && drop && remove && clear && add);
    QCOMPARE(list->parentItem(), drop);
    QTest::qWait(50);
    QVERIFY(list->mapToScene(QPointF(0, list->height())).y() < clear->mapToScene(QPointF()).y());
    QVERIFY(!remove->isEnabled());
    for(const int count:{5,30,1}) {
        QStringList files;for(int i=0;i<count;++i)files<<QString("D:/source-%1.mp4").arg(i);
        sources->setProperty("sourceFiles",files);
        QTRY_VERIFY_WITH_TIMEOUT(drop->mapToScene(QPointF(0,drop->height())).y() <= add->mapToScene(QPointF()).y(),1500);
        QVERIFY(list->mapToScene(QPointF(0,list->height())).y()<add->mapToScene(QPointF()).y());
    }
    sources->setProperty("sourceFiles",QStringList{"D:/one.mp4","D:/two.mp4"});
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

void UiSmokeTests::movementClassesAndExportOrder()
{
    pfui::AppInfo::registerQmlTypes(); QQmlApplicationEngine engine;
    auto* window = loadWindow(engine); QVERIFY(window);
    auto* rail = window->findChild<QObject*>("resultsRail"); QVERIFY(rail);
    const QVariantList records{
        QVariantMap{{"id",0},{"similarity",.9},{"classification","raise/right"}},
        QVariantMap{{"id",1},{"similarity",.8},{"classification","step/left"}},
        QVariantMap{{"id",2},{"similarity",.7},{"classification","head_pose"}}};
    rail->setProperty("results",records); rail->setProperty("selectedRows",QVariantMap{{"0",true},{"1",true},{"2",true}});
    QTest::qWait(100);
    QCOMPARE(rail->property("sortCriterion").toString(),QString("classification"));
    QVERIFY(rail->findChild<QObject*>("resultClassFilterCombo"));
    QVERIFY(!rail->findChild<QObject*>("resultColorPicker"));
    QVERIFY(!rail->findChild<QObject*>("clearResultsColorsButton"));
    auto values = [](const QVariant& value) { return value.value<QJSValue>().toVariant(); };
    const auto visible=values(rail->property("visibleResults")).toList();
    QCOMPARE(visible[0].toMap().value("id").toInt(),2);
    auto* popup=window->findChild<QObject*>("exportDialog"); QVERIFY(popup);
    popup->setProperty("results",records); popup->setProperty("selectedRows",QVariantMap{{"0",true},{"1",true},{"2",true}});
    QCOMPARE(values(popup->property("classOrder")).toStringList(),(QStringList{"head_pose","raise","step"}));
    popup->setProperty("selectedRows",QVariantMap{{"0",true},{"1",true}});
    QCOMPARE(values(popup->property("classOrder")).toStringList(),(QStringList{"raise","step"}));
    popup->setProperty("selectedRows",QVariantMap{{"0",true},{"1",true},{"2",true}});
    QCOMPARE(values(popup->property("classOrder")).toStringList(),(QStringList{"head_pose","raise","step"}));
    // Folder-per-class selection was removed with the compact export flow;
    // exports use the same class grouping in a single destination.
    QVERIFY(!popup->findChild<QObject*>("exportClassFoldersCheck"));
    auto* picker=rail->findChild<QObject*>("resultClassPicker"); QVERIFY(picker);
    QSignalSpy requested(rail,SIGNAL(pairClassRequested(int,QString))); QVERIFY(requested.isValid());
    std::function<QQuickItem*(QQuickItem*)> findClassButton = [&](QQuickItem* item) -> QQuickItem* {
        if(item->objectName()=="resultClassButton0")return item;
        for(auto* child:item->childItems())if(auto* found=findClassButton(child))return found;
        return nullptr;
    };
    auto* classButton=findClassButton(window->contentItem()); QVERIFY(classButton);
    QCOMPARE(classButton->property("width").toInt(), 34);
    auto* classButtonLabel = classButton->findChild<QObject*>("resultClassButtonLabel0"); QVERIFY(classButtonLabel);
    QCOMPARE(classButtonLabel->property("text").toString(), QStringLiteral("Тип"));
    QVERIFY(classButtonLabel->property("width").toReal() > 0.0);
    QVERIFY(classButtonLabel->property("height").toReal() > 0.0);
    QVERIFY(QMetaObject::invokeMethod(classButton,"clicked")); QTRY_VERIFY(picker->property("opened").toBool());
    QVERIFY(picker->property("x").toDouble()>window->width()/2.0);
    QVERIFY(picker->property("x").toDouble()+picker->property("width").toDouble()<=window->width()-7);
    const int originalHeight=window->height();
    window->setHeight(450);
    QTRY_VERIFY(picker->property("height").toDouble() <= std::min(440,window->height()-32));
    window->setHeight(originalHeight);
    std::function<QQuickItem*(QQuickItem*)> findChoice = [&](QQuickItem* item) -> QQuickItem* {
        if (item->objectName() == "resultClassChoice_head_turn") return item;
        for (auto* child : item->childItems()) if (auto* found = findChoice(child)) return found;
        return nullptr;
    };
    auto* choice=findChoice(window->contentItem()); QVERIFY(choice);
    QVERIFY(QMetaObject::invokeMethod(choice,"clicked"));
    QCOMPARE(requested.count(),1); QCOMPARE(requested.takeFirst(),(QList<QVariant>{0,"head_turn"}));
    const QVariantList detailed{
        QVariantMap{{"id",0},{"classification","head_nod"}},
        QVariantMap{{"id",1},{"classification","arms_folded"}},
        QVariantMap{{"id",2},{"classification","raise_lower"}},
        QVariantMap{{"id",3},{"classification","head_front"}},
        QVariantMap{{"id",4},{"classification","reach_return"}},
        QVariantMap{{"id",5},{"classification","obsolete_type"}},
        QVariantMap{{"id",6},{"classification","head_turn"}},
        QVariantMap{{"id",7},{"classification","turn"}}};
    rail->setProperty("results",detailed);QTest::qWait(100);
    const auto grouped=values(rail->property("visibleResults")).toList();
    QCOMPARE(grouped.size(),8);
    const QList<int> expected{6,7,3,1,0,2,4,5};
    for(int i=0;i<expected.size();++i)QCOMPARE(grouped[i].toMap().value("id").toInt(),expected[i]);
    popup->setProperty("results",detailed);
    popup->setProperty("selectedRows",QVariantMap{{"0",true},{"1",true},{"2",true},{"3",true},{"4",true},{"5",true},{"6",true},{"7",true}});
    QCOMPARE(values(popup->property("classOrder")).toStringList(),
        (QStringList{"head_turn","turn","head_front","arms_folded","head_nod","raise_lower","reach_return","unknown"}));
    const QVariantList equivalent{
        QVariantMap{{"id",0},{"classification","step+turn/left"}},
        QVariantMap{{"id",1},{"classification","turn+step/right"}},
        QVariantMap{{"id",2},{"classification","step+step"}},
        QVariantMap{{"id",3},{"classification","step+obsolete_type"}}};
    rail->setProperty("results",equivalent);
    popup->setProperty("results",equivalent);
    popup->setProperty("selectedRows",QVariantMap{{"0",true},{"1",true},{"2",true},{"3",true}});
    QTest::qWait(100);
    QCOMPARE(values(popup->property("classOrder")).toStringList(),(QStringList{"turn+step","unknown"}));
    rail->setProperty("classFilter","turn+step");QTest::qWait(100);
    QCOMPARE(values(rail->property("visibleResults")).toList().size(),2);
    const QVariantList mismatchedPoses{
        QVariantMap{{"id",0},{"classification","pose_mismatch/arms_raised+legs_straight"}},
        QVariantMap{{"id",1},{"classification","pose_mismatch/legs_straight+arms_raised"}}};
    rail->setProperty("results",mismatchedPoses);popup->setProperty("results",mismatchedPoses);
    popup->setProperty("selectedRows",QVariantMap{{"0",true},{"1",true}});QTest::qWait(100);
    QVERIFY(values(rail->property("classChoices")).toStringList().contains("pose_mismatch"));
    rail->setProperty("classFilter","pose_mismatch");QTest::qWait(100);
    QCOMPARE(values(rail->property("visibleResults")).toList().size(),2);
    QCOMPARE(values(popup->property("classOrder")).toStringList(),QStringList{"pose_mismatch"});
    const QVariantList conflictingActions{
        QVariantMap{{"id",0},{"classification","unknown/unknown"}},
        QVariantMap{{"id",1},{"classification","action_mismatch"}},
        QVariantMap{{"id",2},{"classification","head_action_mismatch"}},
        QVariantMap{{"id",3},{"classification","head_turn/right"}},
        QVariantMap{{"id",4},{"classification","seated_lying"}}};
    rail->setProperty("results",conflictingActions);popup->setProperty("results",conflictingActions);
    popup->setProperty("selectedRows",QVariantMap{{"0",true},{"1",true},{"2",true},{"3",true},{"4",true}});QTest::qWait(100);
    QVERIFY(values(rail->property("classChoices")).toStringList().contains("action_mismatch"));
    QVERIFY(values(rail->property("classChoices")).toStringList().contains("head_action_mismatch"));
    QVERIFY(values(rail->property("classChoices")).toStringList().contains("seated_lying"));
    rail->setProperty("classFilter","seated_lying");QTest::qWait(100);
    QCOMPARE(values(rail->property("visibleResults")).toList().size(),1);
    rail->setProperty("classFilter","action_mismatch");QTest::qWait(100);
    QCOMPARE(values(rail->property("visibleResults")).toList().size(),1);
    rail->setProperty("classFilter","head_action_mismatch");QTest::qWait(100);
    QCOMPARE(values(rail->property("visibleResults")).toList().size(),1);
    QCOMPARE(values(popup->property("classOrder")).toStringList(),(QStringList{"seated_lying","head_turn","head_action_mismatch","action_mismatch","unknown"}));
}
void UiSmokeTests::staticPoseDisagreementKeepsBothPoses()
{
    const auto poseWindow=[](const std::function<void(pfcore::PoseFrame&)>& edit) {
        pfcore::MotionWindow window;window.staticFrameSet=true;
        for(int i=0;i<=10;++i) {
            pfcore::PoseFrame frame;frame.timestampSeconds=i*.1;
            frame.keypoints={{0,-2},{-.5,-2.5},{.5,-2.5},{-1,-2},{1,-2},
                {-2,0},{2,0},{-2,1.5},{2,1.5},{-2,3},{2,3},
                {-1,4},{1,4},{-1,6},{1,6},{-1,8},{1,8}};
            if(edit)edit(frame);
            window.frames.push_back(std::move(frame));
        }
        return window;
    };
    const auto classifyPair=[&](const pfcore::MotionWindow& left,const pfcore::MotionWindow& right,bool headOnly) {
        pfcore::MotionMatch match;match.leftStartSeconds=match.rightStartSeconds=0;
        match.leftEndSeconds=match.rightEndSeconds=1;match.headOnlyComparison=headOnly;
        QVariantMap row;
        pfui::classifyResultRange(row,match,{pfui::groupingEvidence(left),pfui::groupingEvidence(right)});
        return row.value("classification").toString();
    };

    const auto armsRaised=poseWindow([](pfcore::PoseFrame& frame) {
        frame.keypoints[9].y=-2;frame.keypoints[10].y=-2;
    });
    const auto straightLegs=poseWindow({});
    QCOMPARE(classifyPair(armsRaised,straightLegs,false),QString("pose_mismatch/arms_raised+legs_straight"));
    QCOMPARE(classifyPair(straightLegs,armsRaised,false),QString("pose_mismatch/arms_raised+legs_straight"));
    QCOMPARE(classifyPair(straightLegs,straightLegs,false),QString("legs_straight"));
    const auto seated=poseWindow([](pfcore::PoseFrame& frame) {
        frame.keypoints[13]={-4,6};frame.keypoints[14]={4,6};
        frame.keypoints[15]={-4,8};frame.keypoints[16]={4,8};
    });
    const auto lying=poseWindow([](pfcore::PoseFrame& frame) {
        frame.keypoints[11]={2,4};frame.keypoints[12]={4,4};
    });
    const auto cropped=poseWindow([](pfcore::PoseFrame& frame) {
        for(int joint=11;joint<17;++joint)frame.keypoints[joint].confidence=.1;
    });
    QCOMPARE(classifyPair(seated,lying,false),QString("seated_lying"));
    QCOMPARE(classifyPair(seated,straightLegs,false),QString("posture_mismatch"));
    QCOMPARE(classifyPair(seated,cropped,false),QString("posture_uncertain"));
    const auto uncertainPose=poseWindow([](pfcore::PoseFrame& frame) {
        for(auto& point:frame.keypoints)point.confidence=0.;
    });
    QCOMPARE(classifyPair(armsRaised,uncertainPose,false),QString("posture_uncertain"));

    auto semanticPosture=[](pfcore::MotionWindow window,const int label) {
        window.sourceId="semantic-source";
        pfcore::VideoActionEvidence evidence;evidence.sourceId=window.sourceId;
        evidence.modelHash=pfcore::kVideoActionModelHash;evidence.start=0;evidence.end=1;
        evidence.observed=evidence.continuous=evidence.associated=evidence.torsoVisible=evidence.postureVisible=true;
        for(int i=0;i<10;++i)evidence.frameTimes.push_back(i*.1);
        evidence.scores[label]=.84;evidence.frozenScores[label]=.74;
        window.videoActions.push_back(std::move(evidence));return window;
    };
    const auto croppedSeated=semanticPosture(cropped,10);
    const auto croppedLying=semanticPosture(cropped,7);
    const auto croppedStanding=semanticPosture(cropped,11);
    QCOMPARE(classifyPair(croppedSeated,croppedLying,false),QString("seated_lying"));
    QCOMPARE(classifyPair(croppedSeated,croppedStanding,false),QString("posture_mismatch"));

    const auto headTurned=poseWindow([](pfcore::PoseFrame& frame){frame.keypoints[0].x=.7;});
    QCOMPARE(classifyPair(straightLegs,headTurned,true),QString("pose_mismatch/head_front+head_turned"));
}

void UiSmokeTests::classificationsFollowEditedRange()
{
    pfcore::MotionWindow window;
    window.sceneStartSeconds=0; window.sceneEndSeconds=3.1;
    window.appearanceEmbedding={1,2,3}; window.sceneSequence={1,2,3};
    for(int i=0;i<=30;++i) {
        pfcore::PoseFrame frame;
        frame.timestampSeconds=i*.1;
        frame.keypoints={{0,-2},{-.5,-2.5},{.5,-2.5},{-1,-2},{1,-2},
            {-2,0},{2,0},{-2,1.5},{2,1.5},{-2,3},{2,3},
            {-1,4},{1,4},{-1,6},{1,6},{-1,8},{1,8}};
        frame.keypoints[9].y-=2*std::max(0.,frame.timestampSeconds-1.5);
        window.frames.push_back(std::move(frame));
    }
    pfui::ResultPoseEvidence evidence{pfui::groupingEvidence(window),pfui::groupingEvidence(window)};
    QVERIFY(evidence[0].appearanceEmbedding.empty()); QVERIFY(evidence[0].sceneSequence.empty());
    pfcore::MotionMatch match;
    match.leftStartSeconds=match.rightStartSeconds=0;
    match.leftEndSeconds=match.rightEndSeconds=3;
    QVariantMap record;
    pfui::classifyResultRange(record,match,evidence);
    QCOMPARE(record.value("classification").toString().section('/',0,0),QString("raise"));
    match.leftEndSeconds=match.rightEndSeconds=1.4;
    pfui::classifyResultRange(record,match,evidence);
    QCOMPARE(record.value("classification").toString().section('/',0,0),QString("static"));
    match.leftEndSeconds=match.rightEndSeconds=3;
    pfui::classifyResultRange(record,match,evidence);
    QCOMPARE(record.value("classification").toString().section('/',0,0),QString("raise"));
    match.leftStartSeconds=match.rightStartSeconds=3.2;
    match.leftEndSeconds=match.rightEndSeconds=4;
    pfui::classifyResultRange(record,match,evidence);
    QCOMPARE(record.value("classification").toString().section('/',0,0),QString("unknown"));
    record.insert("classificationManual",true);record.insert("classification","head_turn");
    pfui::classifyResultRange(record,match,evidence);
    QCOMPARE(record.value("classification").toString(),QString("head_turn"));
    record.insert("classificationManual",false);
    window.headOrientations.clear();
    for(const auto& frame:window.frames) {
        const double angle=std::max(0.,frame.timestampSeconds-1.5)*.6;
        const double c=std::cos(angle),s=std::sin(angle);
        window.headOrientations.push_back({frame.timestampSeconds,{c,0,s,0,1,0,-s,0,c},true});
    }
    evidence={pfui::groupingEvidence(window),pfui::groupingEvidence(window)};
    QCOMPARE(evidence[0].headOrientations.size(),window.headOrientations.size());
    match.headOnlyComparison=false;
    match.leftStartSeconds=match.rightStartSeconds=0;
    match.leftEndSeconds=match.rightEndSeconds=3;
    pfui::classifyResultRange(record,match,evidence);
    QCOMPARE(record.value("classification").toString().section('/',0,0),QString("raise+head_turn_away"));
    QCOMPARE(record.value("classificationActions").toStringList(),(QStringList{"raise","head_turn"}));
    record.insert("classificationManual",true);record.insert("classification","step");
    pfui::classifyResultRange(record,match,evidence);
    QCOMPARE(record.value("classification").toString(),QString("step"));
    record.insert("classificationManual",false);
    match.headOnlyComparison=true;
    match.leftStartSeconds=match.rightStartSeconds=0;
    match.leftEndSeconds=match.rightEndSeconds=3;
    pfui::classifyResultRange(record,match,evidence);
    QCOMPARE(record.value("classification").toString().section('/',0,0),QString("head_turn_away"));
    // A return and an outward turn must not inherit one side's specific phase.
    auto returning=window;
    for(auto& sample:returning.headOrientations) {
        const double angle=std::max(0.,1.5-sample.time)*.6;
        const double c=std::cos(angle),s=std::sin(angle);
        sample.rotation={c,0,s,0,1,0,-s,0,c};
    }
    evidence[1]=pfui::groupingEvidence(returning);
    pfui::classifyResultRange(record,match,evidence);
    QCOMPARE(record.value("classification").toString().section('/',0,0),QString("head_turn"));
    evidence[1]=pfui::groupingEvidence(window);
    match.leftEndSeconds=match.rightEndSeconds=1.4;
    pfui::classifyResultRange(record,match,evidence);
    QCOMPARE(record.value("classification").toString().section('/',0,0),QString("static"));
    // Observed head phases also apply when the full body is visible, without
    // turning an independent raised arm into head-only evidence.
    for(auto& frame:window.frames)frame.keypoints[9].y=3;
    for(auto& frame:returning.frames)frame.keypoints[9].y=3;
    match.headOnlyComparison=false;
    match.leftEndSeconds=match.rightEndSeconds=3;
    evidence={pfui::groupingEvidence(window),pfui::groupingEvidence(window)};
    pfui::classifyResultRange(record,match,evidence);
    QCOMPARE(record.value("classification").toString().section('/',0,0),QString("head_turn_away"));
    evidence={pfui::groupingEvidence(returning),pfui::groupingEvidence(returning)};
    pfui::classifyResultRange(record,match,evidence);
    QCOMPARE(record.value("classification").toString().section('/',0,0),QString("head_turn_back"));
    evidence[1]=pfui::groupingEvidence(window);
    pfui::classifyResultRange(record,match,evidence);
    QCOMPARE(record.value("classification").toString().section('/',0,0),QString("head_turn"));
    // Compound learned head actions retain their grouping facets and phase.
    for(auto& sample:window.headOrientations) {
        const double t=std::min(1.,sample.time/3),yaw=.9*t,pitch=.35*t;
        const double cy=std::cos(yaw),sy=std::sin(yaw),cp=std::cos(pitch),sp=std::sin(pitch);
        sample.rotation={cy,sy*sp,sy*cp,0,cp,-sp,-sy,cy*sp,cy*cp};
    }
    evidence={pfui::groupingEvidence(window),pfui::groupingEvidence(window)};
    for(bool headOnly:{false,true}) {
        match.headOnlyComparison=headOnly;
        pfui::classifyResultRange(record,match,evidence);
        QCOMPARE(record.value("classification").toString().section('/',0,0),QString("head_turn_away+head_tilt"));
        QCOMPARE(record.value("classificationActions").toStringList(),(QStringList{"head_turn","head_tilt"}));
    }
    for(auto& frame:window.frames)for(int joint=7;joint<17;++joint)frame.keypoints[joint].confidence=.01;
    evidence={pfui::groupingEvidence(window),pfui::groupingEvidence(window)};
    match.headOnlyComparison=false;
    pfui::classifyResultRange(record,match,evidence);
    QCOMPARE(record.value("classification").toString().section('/',0,0),QString("head_turn_away+head_tilt"));
    QVERIFY(record.value("classificationPartial").toBool());
    record.insert("classificationManual",true);record.insert("classification","step");
    pfui::classifyResultRange(record,match,evidence);
    QCOMPARE(record.value("classification").toString(),QString("step"));
    record.insert("classificationManual",false);
    match.headOnlyComparison=true;
    for(auto& sample:evidence[1].headOrientations)sample.rotation={1,0,0,0,1,0,0,0,1};
    pfui::classifyResultRange(record,match,evidence);
    QCOMPARE(record.value("classification").toString(),QString("head_action_mismatch"));
    QVERIFY(record.value("classificationConflict").toBool());
    for(auto& sample:evidence[1].headOrientations)sample.observed=false;
    pfui::classifyResultRange(record,match,evidence);
    QCOMPARE(record.value("classification").toString().section('/',0,0),QString("unknown"));
    QVERIFY(!record.value("classificationConflict").toBool());
}

void UiSmokeTests::previewRequestsRenderBySourceAndTimeWithoutReindexing()
{
    const std::vector<pfui::PreviewRequestOrderKey> requests{
        {"source-b.mp4", 9.0}, {"source-a.mp4", 8.0}, {"source-b.mp4", 1.0},
        {"source-a.mp4", 3.0}, {"source-a.mp4", 3.0}, {"source-b.mp4", 4.0}};
    const auto order = pfui::previewRenderOrder(requests);
    QCOMPARE(order, (std::vector<std::size_t>{3, 4, 1, 2, 5, 0}));
    for (std::size_t position = 1; position < order.size(); ++position) {
        const auto& previous = requests[order[position - 1]];
        const auto& current = requests[order[position]];
        QVERIFY(previous.source <= current.source);
        if (previous.source == current.source)
            QVERIFY(previous.seconds <= current.seconds);
    }
}

void UiSmokeTests::mcpSettingsExposeWorkingConnection()
{
    pfui::AppInfo::registerQmlTypes();QQmlApplicationEngine engine;auto* window=loadWindow(engine);QVERIFY(window);
    auto* dialog=window->findChild<QObject*>("settingsDialog");QVERIFY(dialog);
    auto* tabs=window->findChild<QObject*>("settingsTabs");QVERIFY(tabs);tabs->setProperty("currentIndex",0);
    QVERIFY(QMetaObject::invokeMethod(dialog,"open"));
    auto* mcp=engine.singletonInstance<pfui::McpController*>(qmlTypeId("PfUiBridge",1,0,"Mcp"));QVERIFY(mcp);
    const bool wasEnabled=mcp->enabled(),wasControl=mcp->allowControl();const int wasPort=mcp->port();
    const auto restore=qScopeGuard([&]{mcp->setEnabled(false);mcp->setPort(wasPort);mcp->setAllowControl(wasControl);mcp->setEnabled(wasEnabled);pfservices::SettingsStore::flushPendingWrites();});
    mcp->setEnabled(false);QTcpServer portProbe;QVERIFY(portProbe.listen(QHostAddress::LocalHost,0));const int port=portProbe.serverPort();portProbe.close();mcp->setPort(port);
    auto* toggle=window->findChild<QObject*>("mcpEnabledCheck");auto* control=window->findChild<QObject*>("mcpControlCheck");auto* copy=window->findChild<QObject*>("mcpCopyConnection");QVERIFY(toggle);QVERIFY(control);QVERIFY(copy);
    QVERIFY(!copy->property("enabled").toBool());mcp->setEnabled(true);QVERIFY(mcp->running());QTRY_VERIFY(toggle->property("checked").toBool());QTRY_VERIFY(copy->property("enabled").toBool());
    mcp->copyConnection();const auto config=QJsonDocument::fromJson(QGuiApplication::clipboard()->text().toUtf8()).object()["mcpServers"].toObject()["parallel-finder"].toObject();
    QCOMPARE(config["url"].toString(),QString("http://127.0.0.1:%1/mcp").arg(port));QVERIFY(config["headers"].toObject()["Authorization"].toString().startsWith("Bearer "));
    mcp->setAllowControl(true);QTRY_VERIFY(control->property("checked").toBool());mcp->setEnabled(false);QTRY_VERIFY(!toggle->property("checked").toBool());QVERIFY(!mcp->running());
    QGuiApplication::clipboard()->clear();
}

void UiSmokeTests::largeResultSetKeepsClassesContiguous()
{
    pfui::AppInfo::registerQmlTypes(); QQmlApplicationEngine engine;
    auto* window=loadWindow(engine); QVERIFY(window);
    auto* rail=window->findChild<QObject*>("resultsRail"); QVERIFY(rail);
    auto* popup=window->findChild<QObject*>("exportDialog"); QVERIFY(popup);
      const QStringList classes{"head_turn_away","head_turn_back","head_turn","head_turn+raise","turn","head_front","arms_folded","head_nod","raise","step","mixed","unknown"};
    QVariantList rows,indexes;QVariantMap selected;std::vector<pfcore::MotionMatch> matches(417);
    for(int id=0;id<417;++id) {
        rows.push_back(QVariantMap{{"id",id},{"classification",classes[(id*7)%classes.size()]},
            {"similarity",double((id*13)%100)/100},{"classificationPartial",id==0}});
        indexes.push_back(id);selected.insert(QString::number(id),true);
        matches[id].leftSourceId="source.mp4";matches[id].leftStartSeconds=417-id;
    }
    rail->setProperty("results",rows);popup->setProperty("results",rows);popup->setProperty("selectedRows",selected);
    rail->setProperty("selectedIndex",0);
    QTRY_VERIFY(!rail->property("bulkUpdating").toBool());
    std::function<QQuickItem*(QQuickItem*)> findPartialLabel=[&](QQuickItem* item)->QQuickItem* {
        if(item->objectName()=="resultManualClassLabel0")return item;
        for(auto* child:item->childItems())if(auto* found=findPartialLabel(child))return found;
        return nullptr;
    };
    auto* partialLabel=findPartialLabel(window->contentItem());QVERIFY(partialLabel);
    QVERIFY(partialLabel->isVisible());
    QVERIFY(partialLabel->property("text").toString().contains("Head motion")
        || partialLabel->property("text").toString().contains(QString::fromUtf8("движение головы")));
    const auto visible=rail->property("visibleResults").value<QJSValue>().toVariant().toList();
    QCOMPARE(visible.size(),417);
    int previous=-1;
    for(const auto& value:visible) {
        const int rank=classes.indexOf(value.toMap().value("classification").toString());
        QVERIFY(rank>=previous);previous=rank;
    }
    const auto exportClasses=popup->property("classOrder").value<QJSValue>().toVariant().toStringList();
    QCOMPARE(exportClasses,classes);
    const auto ordered=pfui::orderedExportIndexes(indexes,matches,rows,0,exportClasses);
    QCOMPARE(ordered.size(),std::size_t(417));previous=-1;
    for(const int id:ordered) {
        const int rank=classes.indexOf(rows[id].toMap().value("classification").toString());
        QVERIFY(rank>=previous);previous=rank;
    }
    // Editing a selected category must regroup it without following it down
    // the list. Manual labels must also fit inside the card at narrow widths.
    auto* list=window->findChild<QQuickItem*>("resultList"); QVERIFY(list);
    rail->setProperty("selectedIndex",0);
    QTest::qWait(100);
    list->setProperty("contentY",180.0);
    const double viewportY=list->property("contentY").toDouble();
    auto changed=rows[0].toMap();changed.insert("classification","step");rows[0]=changed;
    changed.insert("classificationManual",true);rows[0]=changed;
    rail->setProperty("results",rows);popup->setProperty("results",rows);
    QTest::qWait(300);
    QVERIFY2(std::abs(list->property("contentY").toDouble()-viewportY)<2.0,
             "Reclassification must not scroll to the relocated selected card");
    rail->setProperty("selectedIndex",1);
    QTest::qWait(100);
    rail->setProperty("selectedIndex",0);
    QTest::qWait(300);
    std::function<QQuickItem*(QQuickItem*)> findCard=[&](QQuickItem* item)->QQuickItem* {
        if(item->objectName()=="resultCard0")return item;
        for(auto* child:item->childItems())if(auto* found=findCard(child))return found;
        return nullptr;
    };
    auto* card=findCard(window->contentItem()); QVERIFY(card);
    auto* label=card->findChild<QQuickItem*>("resultManualClassLabel0"); QVERIFY(label);
    QVERIFY(label->isVisible());
    const auto labelPosition=label->mapToItem(card,QPointF(0,label->height()));
    QVERIFY(labelPosition.y()<=card->height()-8);
    for(auto* item:card->findChildren<QQuickItem*>()) {
        if(item->inherits("QQuickText") && item->isVisible())
            QVERIFY2(item->mapToItem(card,QPointF(0,item->height())).y()<=card->height()-7,
                     "Card text must fit inside its content padding");
    }
    const auto edited=rail->property("visibleResults").value<QJSValue>().toVariant().toList();previous=-1;
    for(const auto& value:edited) {
        const int rank=classes.indexOf(value.toMap().value("classification").toString());
        QVERIFY(rank>=previous);previous=rank;
    }
}

void UiSmokeTests::exportClassOrderControlsFileNumbering()
{
    std::vector<pfcore::MotionMatch> matches(5);
    for (int i=0;i<5;++i) { matches[i].leftSourceId="one.mp4"; matches[i].leftStartSeconds=50-i*10; }
    const QVariantList records{
        QVariantMap{{"classification","raise/right"}}, QVariantMap{{"classification","step/left"}},
        QVariantMap{{"classification","raise/left"}}, QVariantMap{}, QVariantMap{{"classification","wave"}}};
    const QVariantList indexes{0,1,2,3,4,1,-1,50,"invalid"};
    const QStringList colors{"step","raise","unknown"};
    // Types omitted by an external caller share the unknown group; preserve
    // the requested chronological numbering inside that fallback group.
    QCOMPARE(pfui::orderedExportIndexes(indexes,matches,records,0,colors), (std::vector<int>{1,2,0,4,3}));
    QCOMPARE(pfui::orderedExportIndexes(indexes,matches,records,1,colors), (std::vector<int>{1,0,2,3,4}));
    QCOMPARE(pfui::orderedExportIndexes(indexes,matches,records,0,{}), (std::vector<int>{4,3,2,1,0}));
    QCOMPARE(pfui::orderedExportIndexes(indexes,matches,records,1,{}), (std::vector<int>{0,1,2,3,4}));
    const QVariantList compoundRecords{
        QVariantMap{{"classification","turn+step/away"}},
        QVariantMap{{"classification","step/left"}},
        QVariantMap{{"classification","step+turn/toward"},{"classificationManual",true}},
        QVariantMap{{"classification","unknown"}},
        QVariantMap{{"classification","head_turn+raise/right"}}};
    const QStringList compoundOrder{"head_turn+raise","turn+step","step","unknown"};
    QCOMPARE(pfui::exportClassKey("step+turn/toward",compoundOrder),QString("turn+step"));
    QCOMPARE(pfui::exportClassKey("step+step",compoundOrder),QString("unknown"));
    QCOMPARE(pfui::orderedExportIndexes(indexes,matches,compoundRecords,0,compoundOrder),
             (std::vector<int>{4,2,0,1,3}));
    QCOMPARE(pfui::orderedExportIndexes(indexes,matches,compoundRecords,1,compoundOrder),
             (std::vector<int>{4,0,2,1,3}));
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
    input.start(); wait(1000); input.stop(); disconnect(connection);
    qInfo("Settings input: drag=%d frames max_gap=%lld ms; scroll=%d frames max_gap=%lld ms",dragFrames,static_cast<long long>(dragGap),frames,static_cast<long long>(maxGap));
    QVERIFY(flick->property("contentY").toDouble()>20);
    QVERIFY(dragFrames>=20); QVERIFY(frames>=10);
    QVERIFY(dragGap<100); QVERIFY(maxGap<100);
}

void UiSmokeTests::appearanceRefreshesBackdropBeforeClosing()
{
    pfui::AppInfo::registerQmlTypes(); QQmlApplicationEngine engine;
    auto* window=loadWindow(engine); QVERIFY(window);
    auto* popup=window->findChild<QObject*>("settingsDialog"); QVERIFY(popup);
    auto* button=window->findChild<QQuickItem*>("sourcePrimaryAction"); QVERIFY(button);
    QVERIFY(!window->findChild<QObject*>("modalBackdropSnapshot"));
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
    QEventLoop loop;
    // Queued frame notifications must die with the local loop, before the
    // timestamps/clock leave scope and engine destruction emits more frames.
    connect(window, &QQuickWindow::frameSwapped, &loop, [&] { timestamps.push_back(elapsed.elapsed()); }, Qt::QueuedConnection);
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
    QSignalSpy opened(rail.get(), SIGNAL(pairOpened(int))); QVERIFY(opened.isValid());
    QVERIFY(QMetaObject::invokeMethod(rail.get(), "selectNext"));
    QCOMPARE(rail->property("selectedIndex").toInt(), 2);
    QCOMPARE(opened.count(), 1); QCOMPARE(opened.last().first().toInt(), 2);
    QVERIFY(QMetaObject::invokeMethod(rail.get(), "selectNext"));
    QCOMPARE(rail->property("selectedIndex").toInt(), 1);
    QCOMPARE(opened.count(), 2); QCOMPARE(opened.last().first().toInt(), 1);
    QVERIFY(QMetaObject::invokeMethod(rail.get(), "selectNext"));
    QCOMPARE(rail->property("selectedIndex").toInt(), 1);
    QCOMPARE(opened.count(), 2);
    QVERIFY(QMetaObject::invokeMethod(rail.get(), "selectPrevious"));
    QCOMPARE(rail->property("selectedIndex").toInt(), 2);
    QCOMPARE(opened.count(), 3); QCOMPARE(opened.last().first().toInt(), 2);
    QVERIFY(QMetaObject::invokeMethod(rail.get(), "selectPrevious"));
    QCOMPARE(rail->property("selectedIndex").toInt(), 2);
    QCOMPARE(opened.count(), 3);
    QVERIFY(QMetaObject::invokeMethod(rail.get(), "selectNext"));
    QVERIFY(QMetaObject::invokeMethod(rail.get(), "goToTop"));
    QCOMPARE(rail->property("selectedIndex").toInt(), 2);
    QCOMPARE(opened.count(), 5); QCOMPARE(opened.last().first().toInt(), 2);
}


void UiSmokeTests::reviewRemovalKeepsNeighborAndColorsRefresh()
{
    pfui::AppInfo::registerQmlTypes();
    QQmlApplicationEngine engine;
    QQmlComponent component(&engine);
    component.setData("import QtQuick\nimport PfUi\nResultsRail { width: 280; height: 500; onResultSelected: function(id) { selectedIndex = id } }", QUrl());
    std::unique_ptr<QObject> rail(component.create());
    QVERIFY2(rail != nullptr, qPrintable(component.errorString()));
    auto record = [](int id, bool hidden = false) { return QVariantMap{{"id",id},{"similarity",1.0-id*.01},{"hidden",hidden},{"category","Red"},{"categoryColor","#638edb"}}; };
    rail->setProperty("results", QVariantList{record(0),record(1),record(2),record(3),record(4)});
    rail->setProperty("selectedIndex",2);
    rail->setProperty("results", QVariantList{record(0),record(1),record(2,true),record(3),record(4)});
    QCOMPARE(rail->property("selectedIndex").toInt(),3);
    rail->setProperty("results", QVariantList{record(0),record(1),record(2,true),record(3,true),record(4,true)});
    QCOMPARE(rail->property("selectedIndex").toInt(),1);
    rail->setProperty("results", QVariantList{record(0),record(1),record(2),record(3),record(4)});
    rail->setProperty("selectedIndex",2);
    rail->setProperty("results", QVariantList{record(0,true),record(1,true),record(2,true),record(3),record(4)});
    QCOMPARE(rail->property("selectedIndex").toInt(),3);
    rail->setProperty("results", QVariantList{record(0),record(1)});
    rail->setProperty("selectedIndex",1);
    rail->setProperty("sortDescending",false);
    QCOMPARE(rail->property("selectedIndex").toInt(),1);
    auto* l10n = engine.singletonInstance<QObject*>(qmlTypeId("PfUi",1,0,"L10n")); QVERIFY(l10n);
    QVariant label;
    l10n->setProperty("language","en");
    QVERIFY(QMetaObject::invokeMethod(l10n,"colorLabel",Q_RETURN_ARG(QVariant,label),Q_ARG(QVariant,"#638edb")));
    QCOMPARE(label.toString(),QString("Blue"));
    l10n->setProperty("language","ru");
    QVERIFY(QMetaObject::invokeMethod(l10n,"colorLabel",Q_RETURN_ARG(QVariant,label),Q_ARG(QVariant,"#7c9885")));
    QCOMPARE(label.toString(),QString::fromUtf8("Зелёный"));

}

void UiSmokeTests::sourceSearchAndUnreviewedNavigation()
{
    pfui::AppInfo::registerQmlTypes(); QQmlApplicationEngine engine; QQmlComponent component(&engine);
    component.setData("import QtQuick\nimport PfUi\nResultsRail { width: 286; height: 800; onResultSelected: function(id) { selectedIndex = id } }",QUrl());
    std::unique_ptr<QObject> rail(component.create()); QVERIFY2(rail,qPrintable(component.errorString()));
    const auto values = [](const QVariant& value) { return value.metaType() == QMetaType::fromType<QJSValue>() ? value.value<QJSValue>().toVariant() : value; };
    QSignalSpy opened(rail.get(), SIGNAL(pairOpened(int))); QVERIFY(opened.isValid());
    QVariantList rows{
        QVariantMap{{"id",0},{"similarity",.9},{"reviewed",true},{"leftSource","D:/clips/Alpha.mp4"},{"rightSource","D:/clips/Beta.mp4"}},
        QVariantMap{{"id",1},{"similarity",.8},{"leftSource","D:/clips/Gamma.mp4"},{"rightSource","D:/clips/Alpha.mp4"}},
        QVariantMap{{"id",2},{"similarity",.7},{"leftSource","D:/clips/Beta.mp4"},{"rightSource","D:/clips/Gamma.mp4"}}};
    rail->setProperty("results",rows); rail->setProperty("selectedIndex",0);
    QVERIFY(QMetaObject::invokeMethod(rail.get(),"selectNextUnreviewed")); QCOMPARE(rail->property("selectedIndex").toInt(),1);
    QCOMPARE(opened.count(),1); QCOMPARE(opened.last().first().toInt(),1);
    QVERIFY(QMetaObject::invokeMethod(rail.get(),"selectNextUnreviewed")); QCOMPARE(rail->property("selectedIndex").toInt(),2);
    QCOMPARE(opened.count(),2); QCOMPARE(opened.last().first().toInt(),2);
    QVERIFY(QMetaObject::invokeMethod(rail.get(),"selectNextUnreviewed")); QCOMPARE(rail->property("selectedIndex").toInt(),1);
    QCOMPARE(opened.count(),3); QCOMPARE(opened.last().first().toInt(),1);
    const int openedBeforeFilterChanges = opened.count();
    rail->setProperty("sourceQuery","ALPHA");
    QCOMPARE(values(rail->property("visibleResults")).toList().size(),2);
    QCOMPARE(opened.count(),openedBeforeFilterChanges);
    QSignalSpy selected(rail.get(),SIGNAL(exportSelectionChanged(QVariant))); QVERIFY(selected.isValid());
    QVERIFY(QMetaObject::invokeMethod(rail.get(),"selectAll"));
    const auto selection = values(selected.last()[0]).toMap(); QCOMPARE(selection.size(),2); QVERIFY(selection.contains("0") && selection.contains("1"));
    rail->setProperty("sourceQuery",""); rail->setProperty("sourceFilter","D:/clips/Gamma.mp4");
    const auto filtered = values(rail->property("visibleResults")).toList(); QCOMPARE(filtered.size(),2);
    QCOMPARE(filtered[0].toMap().value("id").toInt(),1); QCOMPARE(filtered[1].toMap().value("id").toInt(),2);
    QCOMPARE(opened.count(),openedBeforeFilterChanges);
    for (auto& row : rows) { auto record = row.toMap(); record.insert("reviewed",true); row = record; }
    rail->setProperty("results",rows); const int current = rail->property("selectedIndex").toInt();
    QVERIFY(QMetaObject::invokeMethod(rail.get(),"selectNextUnreviewed")); QCOMPARE(rail->property("selectedIndex").toInt(),current);
    QVERIFY(!rail->property("reviewNotice").toString().isEmpty());
    QCOMPARE(opened.count(),openedBeforeFilterChanges);

    auto* review = engine.singletonInstance<QObject*>(qmlTypeId("PfUi",1,0,"Review")); QVERIFY(review);
    QVERIFY(QMetaObject::invokeMethod(review,"setMulti",Q_ARG(QVariant,false)));
    QVERIFY(QMetaObject::invokeMethod(rail.get(),"selectCard",Q_ARG(QVariant,1),Q_ARG(QVariant,0)));
    QCOMPARE(opened.count(),openedBeforeFilterChanges + 1);
    QCOMPARE(opened.last().first().toInt(),1);

    QVariantList manyRows;
    for (int id = 0; id < 24; ++id)
        manyRows.push_back(QVariantMap{{"id",id},{"similarity",.9 - id * .01},{"leftSource","A.mp4"},{"rightSource","B.mp4"}});
    rail->setProperty("sourceFilter",""); rail->setProperty("height",320); rail->setProperty("results",manyRows);
    auto* list = rail->findChild<QObject*>("resultList"); QVERIFY(list);
    QTRY_VERIFY(list->property("contentHeight").toReal() > list->property("height").toReal());
    const int openedBeforeScroll = opened.count();
    list->setProperty("contentY",40.0);
    QTRY_VERIFY(list->property("contentY").toReal() > 0.0);
    QCOMPARE(opened.count(),openedBeforeScroll);
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
    auto* filters = window->findChild<QObject*>("resultFiltersMenu");
    QVERIFY(filters);
    QVERIFY(QMetaObject::invokeMethod(filters, "open"));
    QTRY_VERIFY(filters->property("opened").toBool());
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
    pfui::AppInfo::registerQmlTypes();
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
    QVERIFY(rail->findChild<QObject*>("openResultSelectionButton"));
    QVERIFY(rail->findChild<QObject*>("openResultFiltersButton"));
    QVERIFY(rail->findChild<QObject*>("resultSelectionMenu"));
    QVERIFY(rail->findChild<QObject*>("resultFiltersMenu"));
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
    QTRY_VERIFY(!rail->property("bulkUpdating").toBool());
    auto* list = rail->findChild<QObject*>("resultList"); QVERIFY(list);
    QCOMPARE(list->property("count").toInt(), 200);
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
    QCOMPARE(popup->property("selectedCutMode").toInt(), 0);
    // Cut mode and numbering are intentionally fixed: exports use exact
    // matched boundaries and the same stable classification order as results.
    QCOMPARE(popup->property("selectedCutMode").toInt(), 0);
    QVERIFY(!popup->findChild<QQuickItem*>("fastCutButton"));
    QVERIFY(!popup->findChild<QQuickItem*>("exactCutButton"));
    auto* quality = popup->findChild<QObject*>("exportQualityMode"); QVERIFY(quality);
    QCOMPARE(quality->property("count").toInt(), 3);
    auto* merge = popup->findChild<QObject*>("mergeChronologicalCheck");
    QVERIFY(merge);
    merge->setProperty("checked", true);
    QVERIFY(popup->property("mergeChronological").toBool());
    QCOMPARE(popup->property("selectedCutMode").toInt(), 0);
    const auto capture = qEnvironmentVariable("PF_UI_CAPTURE_DIR");
    if (!capture.isEmpty()) { QDir().mkpath(capture); QTest::qWait(150); QVERIFY(window->grabWindow().save(capture + "/montage-export.png")); }
    merge->setProperty("checked", false);
    quality->setProperty("currentIndex", 0);
    QVERIFY(QMetaObject::invokeMethod(quality, "activated", Q_ARG(int, 0)));
    QVERIFY(popup->property("losslessExport").toBool());
    QCOMPARE(popup->property("selectedCutMode").toInt(), 2);
    merge->setProperty("checked", true);
    QCOMPARE(popup->property("selectedCutMode").toInt(), 2);
    QVERIFY(quality->property("enabled").toBool()); // Can switch back from lossless.
    QVERIFY(QMetaObject::invokeMethod(quality, "activated", Q_ARG(int, 1)));
    QCOMPARE(popup->property("selectedCutMode").toInt(), 0);
}

void UiSmokeTests::exportReopenClearsCancelledStatus()
{
    pfui::AppInfo::registerQmlTypes(); QQmlApplicationEngine engine;
    auto* window = loadWindow(engine); QVERIFY(window);
    auto* popup = window->findChild<QObject*>("exportDialog"); QVERIFY(popup);
    QVERIFY(QMetaObject::invokeMethod(popup, "open"));
    QTRY_VERIFY(popup->property("opened").toBool());
    pfui::AnalysisController::instance()->exportFinished(false, "export cancelled");
    QCOMPARE(popup->property("exportStatus").toString(), QString("export cancelled"));
    QVERIFY(QMetaObject::invokeMethod(popup, "close"));
    QTRY_VERIFY(!popup->property("visible").toBool());
    QVERIFY(QMetaObject::invokeMethod(popup, "open"));
    QTRY_VERIFY(popup->property("opened").toBool());
    QVERIFY(popup->property("exportStatus").toString().isEmpty());
}

void UiSmokeTests::movementFilterRespectsSelectionAndVisibleRows()
{
    pfui::AppInfo::registerQmlTypes(); QQmlApplicationEngine engine;
    auto* window=loadWindow(engine); QVERIFY(window);
    auto* rail=window->findChild<QObject*>("resultsRail"); QVERIFY(rail);
    rail->setProperty("results",QVariantList{
        QVariantMap{{"id",0},{"classification","raise/right"}},
        QVariantMap{{"id",1},{"classification","step/left"}},
        QVariantMap{{"id",2},{"classification","raise/left"}},
        QVariantMap{{"id",3},{"classification","raise/right"},{"hidden",true}}});
    rail->setProperty("selectedRows",QVariantMap{{"0",true},{"1",true},{"2",true},{"3",true}});
    rail->setProperty("classFilter","raise");
    QCOMPARE(rail->property("selectedCount").toInt(),2);
    QVariant exported; QVERIFY(QMetaObject::invokeMethod(rail,"exportRows",Q_RETURN_ARG(QVariant,exported)));
    const auto rows=exported.value<QJSValue>().toVariant().toMap();
    QVERIFY(rows.contains("0")); QVERIFY(rows.contains("2")); QVERIFY(!rows.contains("1")); QVERIFY(!rows.contains("3"));
    rail->setProperty("classFilter","step"); QCOMPARE(rail->property("selectedCount").toInt(),1);
    rail->setProperty("results",QVariantList{QVariantMap{{"id",2},{"classification","raise"}}});
    QTRY_VERIFY(rail->property("classFilter").toString().isEmpty());
}
void UiSmokeTests::largeExportBackdropKeepsWorkspaceGeometry()
{
    pfui::AppInfo::registerQmlTypes(); QQmlApplicationEngine engine;
    auto* window = loadWindow(engine); QVERIFY(window);
    auto* rail = window->findChild<QQuickItem*>("resultsRail"); QVERIFY(rail);
    auto* sources = window->findChild<QQuickItem*>("sourcesRail"); QVERIFY(sources);
    auto* popup = window->findChild<QObject*>("exportDialog"); QVERIFY(popup);
    auto* backdrop = window->findChild<QQuickItem*>("modalBackdrop"); QVERIFY(backdrop);
    QVERIFY(!window->findChild<QObject*>("modalBackdropSnapshot"));
    QVariantList records; QVariantMap selected;
    for (int i = 0; i < 600; ++i) {
        records.push_back(QVariantMap{{"id", i}, {"similarity", .8}, {"categoryColor", "#d56565"}});
        selected.insert(QString::number(i), true);
    }
    rail->setProperty("results", records);
    popup->setProperty("results", records); popup->setProperty("selectedRows", selected);
    QVERIFY(QMetaObject::invokeMethod(popup, "open"));
    QTRY_VERIFY(popup->property("opened").toBool());
    window->resize(3440, 1440); QTest::qWait(400);
    QCOMPARE(backdrop->width(), window->contentItem()->width());
    QCOMPARE(backdrop->height(), window->contentItem()->height());
    QVERIFY(!QQmlProperty(backdrop, "layer.enabled").read().toBool());
    QVERIFY(sources->width() <= 310); QVERIFY(rail->width() <= 310);
    const auto before = sources->mapToScene(QPointF());
    const auto capture = qEnvironmentVariable("PF_UI_CAPTURE_DIR");
    if (!capture.isEmpty()) { QDir().mkpath(capture); QVERIFY(window->grabWindow().save(capture + "/export-ultrawide-600.png")); }
    QVERIFY(QMetaObject::invokeMethod(popup, "close"));
    QTRY_VERIFY(!popup->property("visible").toBool());
    QCOMPARE(sources->mapToScene(QPointF()), before);
    if (!capture.isEmpty()) QVERIFY(window->grabWindow().save(capture + "/workspace-ultrawide-600.png"));
}

void UiSmokeTests::analysisControlsAreUnifiedAndStatusTranslates()
{
    pfui::AppInfo::registerQmlTypes();
    auto* analysis = pfui::AnalysisController::instance();
    const auto originalProvider = analysis->providerChoice();
    analysis->setProviderChoice("cpu");
    const auto restoreProvider = qScopeGuard([&] { analysis->setProviderChoice(originalProvider); });
    QQmlApplicationEngine engine;
    auto* window = loadWindow(engine);
    QVERIFY(window);
    auto* sources = window->findChild<QObject*>("sourcesRail");
    QVERIFY(sources);
    QVERIFY(!sources->findChild<QQuickItem*>("advancedSection"));
    auto* model = sources->findChild<QQuickItem*>("poseModelComboBox"); QVERIFY(model);
    QVERIFY(model->isVisible());
    auto* processing = sources->findChild<QQuickItem*>("frameProcessingCard"); QVERIFY(processing);
    QVERIFY(processing->findChild<QQuickItem*>("processingFastButton"));
    QVERIFY(processing->findChild<QQuickItem*>("processingMediumButton"));
    QVERIFY(processing->findChild<QQuickItem*>("processingMaximumButton"));
    QVERIFY(!sources->findChild<QQuickItem*>("accuracyPresetFastButton"));
    QVERIFY(!sources->findChild<QQuickItem*>("accuracyPresetBalancedButton"));
    auto* settings = window->findChild<QObject*>("settingsDialog"); QVERIFY(settings);
    auto* multi = settings->findChild<QQuickItem*>("multiSelectCheck"); QVERIFY(multi);
    auto* hotkeys = settings->findChild<QQuickItem*>("settingsHotkeysTab"); QVERIFY(hotkeys);
    QVERIFY(!hotkeys->findChild<QQuickItem*>("multiSelectCheck"));
    const QString originalQuality = analysis->qualityProfile();
    const QString originalAccuracy = analysis->accuracyPreset();
    const auto restoreModes = qScopeGuard([&] {
        analysis->setQualityProfile(originalQuality);
        analysis->setAccuracyPreset(originalAccuracy);
    });
    auto* maximum = processing->findChild<QQuickItem*>("processingMaximumButton");
    QVERIFY(QMetaObject::invokeMethod(maximum, "clicked"));
    QCOMPARE(analysis->qualityProfile(), QStringLiteral("maximum"));
    QCOMPARE(analysis->accuracyPreset(), QStringLiteral("precise"));
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
    root->setProperty("stage",QString::fromUtf8("Дополнительный поиск: Подготавливаем найденные кандидаты · 2048 / 9000"));
    QCOMPARE(root->property("translated").toString(),QStringLiteral("Additional search: Preparing retrieved candidates · 2048 / 9000"));
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
            const auto visibleText = item->property("text").toString()
                .remove(QRegularExpression(QStringLiteral("<[^>]*>")));
            if (visibleText.startsWith(prefix)) return true;
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
    auto* configure = center->findChild<QObject*>("configurePairButton");
    auto* actions = center->findChild<QObject*>("pairActionsMenu");
    QVERIFY(left && right && pair && configure && actions);
    QVERIFY(configure->property("primary").toBool());
    QVERIFY(configure->property("motionEmphasis").toBool());
    QVERIFY(center->findChild<QObject*>("adjustPairAction"));
    QVERIFY(center->findChild<QObject*>("replacePairAction"));
    QVERIFY(center->findChild<QObject*>("keepOnlyAAction"));
    QVERIFY(center->findChild<QObject*>("keepOnlyBAction"));
    QVERIFY(!center->findChild<QObject*>("composePairButton"));
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
