#include <AnalysisController.h>
#include <AppInfo.h>
#include <UiRuntime.h>
#include <pfservices/SettingsStore.hpp>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QQuickItem>
#include <cmath>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSaveFile>
#include <QtTest/QTest>
#include "../helpers/onnx_two_people.hpp"

class TargetCharacterTests : public QObject {
    Q_OBJECT
private slots:
    void realSeriesIncrementalRestart() {
        const auto manifestPath=qEnvironmentVariable("PF_TEST_REAL_SERIES_MANIFEST");
        if(manifestPath.isEmpty())QSKIP("Opt-in real-source series/restart regression");
        QFile file(manifestPath);QVERIFY(file.open(QIODevice::ReadOnly));
        const auto manifest=QJsonDocument::fromJson(file.readAll()).object();
        const auto phase=qEnvironmentVariable("PF_TEST_REAL_SERIES_PHASE","seed");
        QVERIFY(phase=="seed"||phase=="incremental"||phase=="warm");
        QStringList sources;
        for(const auto value:manifest.value("initialSources").toArray())sources.push_back(value.toString());
        QCOMPARE(sources.size(),8);
        const auto initialSources=sources;
        if(phase!="seed")for(const auto value:manifest.value("addedSources").toArray())sources.push_back(value.toString());
        QCOMPARE(sources.size(),phase=="seed"?8:10);
        for(const auto& source:sources)QVERIFY(QFileInfo::exists(source));
        const auto model=qEnvironmentVariable("PF_MODEL_PATH");QVERIFY(QFileInfo::exists(model));
        pfservices::Settings settings;settings.modelPath=model.toStdString();settings.provider="cuda";
        settings.modelChoice=QFileInfo(model).fileName().toStdString();settings.qualityProfile="fast";
        settings.cachePath=manifest.value("cacheRoot").toString().toStdString();settings.processingThreads=2;
        QVERIFY(!settings.cachePath.empty());
        std::string error;QVERIFY2(pfservices::SettingsStore().save(settings,error),error.c_str());
        pfui::AppInfo::registerQmlTypes();auto* info=pfui::AppInfo::instance();info->initializeBackendsAsync();
        QTRY_VERIFY_WITH_TIMEOUT(!info->backendInitializing(),30000);
        auto* analysis=pfui::AnalysisController::instance();analysis->setProviderChoice("cuda");analysis->setAnalysisMode("combined");
        analysis->analyzeFiles(sources);
        QTRY_VERIFY_WITH_TIMEOUT(analysis->awaitingTarget()||!analysis->busy(),180000);
        if(phase=="seed") {
            QVERIFY2(analysis->awaitingTarget(),qPrintable(analysis->status()));
            QCOMPARE(analysis->targetSelectionSource(),manifest.value("anchor").toString());
            QVERIFY(analysis->selectTargetCharacter(manifest.value("anchorCandidateId").toString()));
            QTRY_VERIFY_WITH_TIMEOUT(analysis->awaitingTarget()||!analysis->busy(),180000);
        }
        if(analysis->awaitingTarget()) {
            qWarning()<<"SERIES_UNEXPECTED_CONFIRMATION"<<analysis->targetSelectionSource()
                      <<QJsonDocument::fromVariant(analysis->targetCandidates()).toJson(QJsonDocument::Compact);
            analysis->stopAnalysis();QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy(),10000);
            QFAIL("Strong-face fixture required an unexpected per-source confirmation");
        }
        QVERIFY2(analysis->analysisCompleted(),qPrintable(analysis->status()));
        QJsonObject report{{"phase",phase},{"analyzedFrames",analysis->analyzedFrames()},
            {"poseDetections",analysis->poseDetectionCount()}};
        QJsonArray summaries;
        for(const auto& source:sources) {
            const auto summary=analysis->summaryForSource(source);
            QVERIFY2(!summary.value("targetCharacterId").toString().isEmpty(),qPrintable(source));
            QCOMPARE(summary.value("targetAutomatic").toBool(),source!=manifest.value("anchor").toString());
            QVERIFY(summary.contains("observationCacheHit"));
            QCOMPARE(summary.value("observationCacheHit").toBool(),phase=="warm"||(phase=="incremental"&&initialSources.contains(source)));
            if(source==manifest.value("minorityTargetRange").toObject().value("source").toString()) {
                const auto presence=summary.value("targetPresenceSeconds").toDouble();
                QVERIFY(presence>1 && presence<=3.1);
                QVERIFY(presence/summary.value("durationSeconds").toDouble()<=.085);
            }
            auto row=QJsonObject::fromVariantMap(summary);row.insert("source",source);summaries.push_back(row);
        }
        report.insert("sources",summaries);
        if(phase=="warm") {QCOMPARE(analysis->analyzedFrames(),0);QCOMPARE(analysis->poseDetectionCount(),0);}
        else QVERIFY(analysis->analyzedFrames()>0);
        const auto output=qEnvironmentVariable("PF_TEST_REAL_SERIES_REPORT");QVERIFY(!output.isEmpty());
        QSaveFile reportFile(output);QVERIFY(reportFile.open(QIODevice::WriteOnly));
        const auto bytes=QJsonDocument(report).toJson();QCOMPARE(reportFile.write(bytes),bytes.size());QVERIFY(reportFile.commit());
        pfservices::SettingsStore::flushPendingWrites();
    }
    void realVideoSelectionScreenshot() {
        const auto source=qEnvironmentVariable("PF_TEST_REAL_CHARACTER_VIDEO");
        if(source.isEmpty())QSKIP("Opt-in real-source character/visual regression");
        const auto model=qEnvironmentVariable("PF_MODEL_PATH"),cache=qEnvironmentVariable("PF_TEST_REAL_CHARACTER_CACHE");
        QVERIFY(QFileInfo::exists(source)&&QFileInfo::exists(model)&&!cache.isEmpty());
        pfservices::Settings settings;settings.modelPath=model.toStdString();settings.provider="cuda";
        settings.modelChoice=QFileInfo(model).fileName().toStdString();settings.qualityProfile="fast";settings.cachePath=cache.toStdString();
        std::string error;QVERIFY2(pfservices::SettingsStore().save(settings,error),error.c_str());
        pfui::AppInfo::registerQmlTypes();auto* appInfo=pfui::AppInfo::instance();appInfo->initializeBackendsAsync();
        QTRY_VERIFY_WITH_TIMEOUT(!appInfo->backendInitializing(),30000);
        auto* analysis=pfui::AnalysisController::instance();analysis->setProviderChoice("cuda");analysis->setAnalysisMode("combined");
        QQmlApplicationEngine engine;engine.addImportPath("qrc:/qt/qml");engine.load(QUrl("qrc:/qt/qml/PfUi/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().front());QVERIFY(window);
        window->setProperty("sourceFiles",QVariantList{source});
        QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy(),10000);
        analysis->analyzeFiles({source});QTRY_VERIFY_WITH_TIMEOUT(analysis->awaitingTarget()||!analysis->busy(),90000);
        if(!analysis->awaitingTarget()&&analysis->analysisCompleted()) {
            analysis->reselectTargetCharacter(source);
            QTRY_VERIFY_WITH_TIMEOUT(analysis->awaitingTarget()||!analysis->busy(),90000);
        }
        QVERIFY2(analysis->awaitingTarget(),qPrintable(analysis->status()));
        int leadGroups=0;QString leadId;
        for(const auto& value:analysis->targetCandidates()) {
            const auto row=value.toMap();
            if(row.value("presence").toDouble()>60&&row.value("tracks").toInt()>30&&!row.value("unresolved").toBool()) {
                ++leadGroups;leadId=row.value("id").toString();
            }
        }
        QCOMPARE(leadGroups,1);
        // The first preparation may be cold after an artifact version change.
        // Verify the zero-inference guarantee on actual cached reselection.
        if(analysis->analyzedFrames()>0 || analysis->poseDetectionCount()>0) {
            QVERIFY(analysis->selectTargetCharacter(leadId));
            QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy(),90000);
            QVERIFY2(analysis->analysisCompleted(),qPrintable(analysis->status()));
            analysis->reselectTargetCharacter(source);
            QTRY_VERIFY_WITH_TIMEOUT(analysis->awaitingTarget()||!analysis->busy(),90000);
            QVERIFY2(analysis->awaitingTarget(),qPrintable(analysis->status()));
        }
        QCOMPARE(analysis->analyzedFrames(),0);QCOMPARE(analysis->poseDetectionCount(),0);
        QTest::qWait(800);QVERIFY(window->grabWindow().save(QCoreApplication::applicationDirPath()+"/kyle-character-selection.png"));
        analysis->stopAnalysis();QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy(),10000);
    }
    void selectionCachedReselectionAndCancellation() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto model=directory.filePath("workflow-pose.onnx");
        QFile file(model); QVERIFY(file.open(QIODevice::WriteOnly));
        const auto data=pftest::twoPeopleModel();
        QCOMPARE(file.write(data.data(),data.size()),qint64(data.size()));file.close();
        qputenv("PF_MODEL_PATH",model.toUtf8());
        pfservices::Settings settings;settings.modelPath=model.toStdString();
        settings.modelChoice="workflow-pose.onnx";settings.provider="cpu";settings.qualityProfile="fast";
        settings.cachePath=directory.filePath("cache").toStdString();settings.processingThreads=1;
        std::string error;
        QVERIFY2(pfservices::SettingsStore().save(settings,error),error.c_str());
        pfui::AppInfo::registerQmlTypes();
        auto* appInfo=pfui::AppInfo::instance();appInfo->initializeBackendsAsync();
        QTRY_VERIFY_WITH_TIMEOUT(!appInfo->backendInitializing(),30000);
        auto* analysis=pfui::AnalysisController::instance();
        const QString source=QStringLiteral(PF_TARGET_VIDEO);
        QQmlApplicationEngine engine;
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        engine.load(QUrl(QStringLiteral("qrc:/qt/qml/PfUi/qml/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().front());
        QVERIFY(window);
        window->setProperty("sourceFiles",QVariantList{source});
        QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy(),10000);
        qunsetenv("PF_ALLOW_UNVERIFIED_TARGETS");
        analysis->analyzeFiles({source});QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy(),10000);
        QVERIFY(!analysis->awaitingTarget());QVERIFY(!analysis->analysisCompleted());
        QVERIFY(analysis->status().contains(QStringLiteral("распознавание персонажей")));
        QCOMPARE(analysis->analyzedFrames(),0);QCOMPARE(analysis->poseDetectionCount(),0);
        qputenv("PF_ALLOW_UNVERIFIED_TARGETS","1");
        analysis->analyzeFiles({source});
        QTRY_VERIFY_WITH_TIMEOUT(analysis->awaitingTarget() || !analysis->busy(),30000);
        QVERIFY2(analysis->awaitingTarget(),qPrintable(analysis->status()));
        auto* selector=window->findChild<QQuickItem*>("targetCharacterPanel");
        QVERIFY(selector && selector->isVisible());
        // Pose-only fixture observations are explicitly unresolved, not new
        // unique identities. They remain inspectable and selectable.
        QVERIFY(selector->property("unresolvedCount").toInt()>0);
        QCOMPARE(selector->property("confirmedCount").toInt(),0);
        auto* list=selector->findChild<QQuickItem*>("targetCharacterList");
        QVERIFY(list);
        QTRY_COMPARE(list->property("count").toInt(),analysis->targetCandidates().size());
        selector->setProperty("showUnresolved",true);
        QTest::qWait(300);
        const auto image=window->grabWindow();
        QVERIFY(!image.isNull());
        QVERIFY(image.save(QCoreApplication::applicationDirPath()+"/target-character-panel.png"));
        const auto center=selector->mapToScene(QPointF(selector->width()/2,selector->height()/2));
        QVERIFY(std::abs(center.x()-window->width()/2)<window->width()*.1);
        QVERIFY(selector->width()>window->width()*.4);
        QVERIFY(!window->findChild<QObject*>("sourcesRail")->findChild<QObject*>("targetCharacterPanel"));
        const auto candidates=analysis->targetCandidates();
        QVERIFY(candidates.size()>=2);
        const auto first=candidates[0].toMap().value("id").toString();
        const auto second=candidates[1].toMap().value("id").toString();
        QVERIFY(!analysis->selectTargetCharacter("foreign"));
        QVERIFY(analysis->selectTargetCharacter(first));
        QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy(),30000);
        QVERIFY2(analysis->analysisCompleted(),qPrintable(analysis->status()));
        QCOMPARE(analysis->summaryForSource(source).value("targetCharacterId").toString(),first);
        analysis->analyzeFiles({source});
        QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy(),30000);
        QVERIFY(analysis->analysisCompleted());
        QCOMPARE(analysis->analyzedFrames(),0);
        QCOMPARE(analysis->poseDetectionCount(),0);
        analysis->setAnalysisMode("static");
        analysis->analyzeFiles({source});
        QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy(),30000);
        QVERIFY(analysis->analysisCompleted());
        QCOMPARE(analysis->analyzedFrames(),0);
        analysis->setAnalysisMode("motion");
        analysis->reselectTargetCharacter(QDir::toNativeSeparators(source));
        QTRY_VERIFY_WITH_TIMEOUT(analysis->awaitingTarget() || !analysis->busy(),30000);
        QVERIFY2(analysis->awaitingTarget(),qPrintable(analysis->status()));
        QVERIFY(analysis->selectTargetCharacter(second));
        QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy(),30000);
        QCOMPARE(analysis->summaryForSource(source).value("targetCharacterId").toString(),second);
        QCOMPARE(analysis->analyzedFrames(),0);
        analysis->reselectTargetCharacter(source);
        QTRY_VERIFY_WITH_TIMEOUT(analysis->awaitingTarget() || !analysis->busy(),30000);
        QVERIFY(analysis->awaitingTarget());analysis->stopAnalysis();
        QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy(),5000);
        QVERIFY(!analysis->awaitingTarget());
        QVERIFY(!analysis->analysisCompleted());
        // A second source with no face evidence requires explicit confirmation,
        // and its manual choice must survive a cached batch run.
        const auto episode=directory.filePath("episode-two.mp4");
        QVERIFY(QFile::copy(source,episode));
        analysis->analyzeFiles({source,episode});
        QTRY_VERIFY_WITH_TIMEOUT(analysis->awaitingTarget() || !analysis->busy(),30000);
        QVERIFY2(analysis->awaitingTarget(),qPrintable(analysis->status()));
        QCOMPARE(analysis->targetSelectionSource(),episode);
        QVERIFY(analysis->selectTargetCharacter(first));
        QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy(),30000);
        QVERIFY(analysis->analysisCompleted());
        QCOMPARE(analysis->summaryForSource(source).value("targetCharacterId").toString(),second);
        QCOMPARE(analysis->summaryForSource(episode).value("targetCharacterId").toString(),first);
        analysis->analyzeFiles({source,episode});
        QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy(),30000);
        QVERIFY(analysis->analysisCompleted());
        QCOMPARE(analysis->analyzedFrames(),0);
        QCOMPARE(analysis->summaryForSource(episode).value("targetCharacterId").toString(),first);
        pfservices::SettingsStore::flushPendingWrites();
    }
};

int main(int argc,char** argv) {
    pfui::configureUiRuntime();QGuiApplication app(argc,argv);
    QCoreApplication::setOrganizationName("ParallelFinderTests");
    QTemporaryDir profile;
    if(!profile.isValid())return 2;
    // Unique app profile keeps the test independent of installed/user settings.
    QCoreApplication::setApplicationName("TargetCharacter-"+QFileInfo(profile.path()).fileName());
    TargetCharacterTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "test_target_character.moc"
