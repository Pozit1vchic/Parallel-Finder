#include <ResultPreviewImages.h>
#include <AnalysisController.h>
#include <AppInfo.h>
#include <UiRuntime.h>
#include <pfservices/SettingsStore.hpp>
#include <pfservices/PfCache.hpp>
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
#include <QElapsedTimer>
#include <QScopeGuard>
#include <QProcess>
#include <QProcessEnvironment>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <map>
#include <QtTest/QTest>
#include "../helpers/onnx_two_people.hpp"

class TargetCharacterTests : public QObject {
    Q_OBJECT
private slots:
    void allPeopleSmokeDoesNotUseSinglePersonAdmission() {
        QTemporaryDir directory;QVERIFY(directory.isValid());
        const auto model=directory.filePath("workflow-pose.onnx");
        QFile file(model);QVERIFY(file.open(QIODevice::WriteOnly));
        const auto data=pftest::twoPeopleModel();
        QCOMPARE(file.write(data.data(),data.size()),qint64(data.size()));file.close();
        const auto second=directory.filePath("second-video.mp4");
        QVERIFY(QFile::copy(QStringLiteral(PF_TARGET_VIDEO),second));
        pfservices::Settings settings;settings.provider="cpu";settings.qualityProfile="fast";
        settings.modelPath=model.toStdString();settings.modelChoice="workflow-pose.onnx";
        settings.processingThreads=1;settings.cacheEnabled=true;
        const auto settingsPath=directory.filePath("settings.json");
        std::string error;QVERIFY2(pfservices::SettingsStore(settingsPath.toStdString()).save(settings,error),error.c_str());
        auto environment=QProcessEnvironment::systemEnvironment();
#ifdef Q_OS_WIN
        environment.insert("QT_QPA_PLATFORM","windows");
#endif
        environment.insert("PF_MODEL_PATH",model);environment.insert("PF_MODEL_ROOT",directory.path());
        environment.insert("PF_PROVIDER","cpu");environment.insert("PF_ANALYSIS_MODE","combined");
        environment.insert("PF_SMOKE_ALL_PEOPLE","1");environment.remove("PF_SMOKE_TARGET_ID");
        environment.insert("PF_BENCHMARK_SETTINGS_PATH",settingsPath);
        environment.insert("PF_BENCHMARK_CACHE_ROOT",directory.filePath("cache"));
        environment.insert("PF_BENCHMARK_MOVEMENT_CACHE_ROOT",directory.filePath("refinement"));
        environment.insert("PF_ANALYSIS_JSON","1");
        environment.insert("PF_DEBUG_ANALYSIS","1");
        QProcess process;process.setProcessEnvironment(environment);
        const auto cleanup=qScopeGuard([&] {if(process.state()!=QProcess::NotRunning){process.kill();process.waitForFinished(5000);}});
        process.start(QStringLiteral(PF_ANALYSIS_SMOKE_EXE),{"--pf-analysis-smoke",QStringLiteral(PF_TARGET_VIDEO),second});
        QVERIFY(process.waitForStarted(5000));QVERIFY(process.waitForFinished(30000));
        const auto output=process.readAllStandardOutput()+process.readAllStandardError();
        const auto diagnostic=QByteArray::number(process.exitCode())+": "+output;
        if(process.exitStatus()!=QProcess::NormalExit) {
            directory.setAutoRemove(false);
            qInfo()<<"Failed smoke inputs retained at"<<directory.path();
        }
        QVERIFY2(process.exitStatus()==QProcess::NormalExit,diagnostic.constData());
        QVERIFY2(process.exitCode()==0,output.constData());
        QVERIFY2(output.contains("analysis smoke ok"),output.constData());
        // This stage is reached only with at least two admitted windows, so
        // a vacuous empty-input success cannot hide the former empty mask access.
        QVERIFY2(output.contains("PF_DEBUG_TIMING matcher_ms="),output.constData());
        // A new process and changed matcher policy must keep both compatible
        // source observations, including their detector/identity/tracker work.
        settings.similarityThreshold=.9;settings.candidateThreshold=.8;
        settings.timeWeight=.7;settings.duplicateWindowSec=.9;
        settings.mirrorPoses=false;settings.normalizeSize=false;settings.expandedSearch=true;
        QVERIFY2(pfservices::SettingsStore(settingsPath.toStdString()).save(settings,error),error.c_str());
        process.start(QStringLiteral(PF_ANALYSIS_SMOKE_EXE),{"--pf-analysis-smoke",QStringLiteral(PF_TARGET_VIDEO),second});
        QVERIFY(process.waitForStarted(5000));QVERIFY(process.waitForFinished(30000));
        const auto repeated=process.readAllStandardOutput()+process.readAllStandardError();
        QVERIFY2(process.exitStatus()==QProcess::NormalExit && process.exitCode()==0,repeated.constData());
        QCOMPARE(repeated.count("complete_cache_hit=1 window_cache_hit=1 decoded=0 view_samples=0 pose_samples=0"),2);
        QVERIFY2(!repeated.contains("cold_pipeline_ms="),repeated.constData());
        // A writable result is still useful when storage fails. The user must
        // learn that this run cannot be reused instead of silently redoing it.
        const auto blockedRoot=directory.filePath("not-a-cache-directory");
        QFile blocker(blockedRoot);QVERIFY(blocker.open(QIODevice::WriteOnly));
        QCOMPARE(blocker.write("keep"),qint64(4));blocker.close();
        environment.insert("PF_BENCHMARK_CACHE_ROOT",blockedRoot);process.setProcessEnvironment(environment);
        process.start(QStringLiteral(PF_ANALYSIS_SMOKE_EXE),{"--pf-analysis-smoke",QStringLiteral(PF_TARGET_VIDEO),second});
        QVERIFY(process.waitForStarted(5000));QVERIFY(process.waitForFinished(30000));
        const auto failedStorage=process.readAllStandardOutput()+process.readAllStandardError();
        QVERIFY2(process.exitStatus()==QProcess::NormalExit && process.exitCode()==0,failedStorage.constData());
        QVERIFY2(failedStorage.contains("cache_write_failures : 2"),failedStorage.constData());
        QVERIFY2(failedStorage.contains("Кеш не сохранён"),failedStorage.constData());
        QVERIFY(QFileInfo::exists(blockedRoot));
    }

    void realVideoCharacterCapture() {
        const auto source=qEnvironmentVariable("PF_TEST_REAL_CHARACTER_VIDEO");
        const auto model=qEnvironmentVariable("PF_MODEL_PATH");
        const auto cache=qEnvironmentVariable("PF_TEST_REAL_CHARACTER_CACHE");
        const auto report=qEnvironmentVariable("PF_TEST_REAL_CHARACTER_REPORT");
        if(source.isEmpty()||model.isEmpty()||cache.isEmpty()||report.isEmpty())
            QSKIP("Opt-in real-video character capture");
        QVERIFY(QFileInfo::exists(source));QVERIFY(QFileInfo::exists(model));
        pfservices::Settings settings;settings.modelPath=model.toStdString();settings.provider="cuda";
        settings.modelChoice=QFileInfo(model).fileName().toStdString();settings.qualityProfile="fast";
        settings.cachePath=cache.toStdString();settings.processingThreads=2;
        std::string error;QVERIFY2(pfservices::SettingsStore().save(settings,error),error.c_str());
        pfui::AppInfo::registerQmlTypes();auto* info=pfui::AppInfo::instance();info->initializeBackendsAsync();
        QTRY_VERIFY_WITH_TIMEOUT(!info->backendInitializing(),30000);
        auto* analysis=pfui::AnalysisController::instance();analysis->setProviderChoice("cuda");
        analysis->setAnalysisMode("combined");analysis->analyzeFiles({source});
        QTRY_VERIFY_WITH_TIMEOUT(analysis->awaitingTarget()||!analysis->busy(),600000);
        QVERIFY2(analysis->awaitingTarget(),qPrintable(analysis->status()));
        std::map<double,QString> frameUrls;
        int unresolved=0;
        for(const auto& candidate:analysis->targetCandidates()) {
            if(candidate.toMap().value("unresolved").toBool())++unresolved;
            double previous=-1;
            for(const auto& value:candidate.toMap().value("previews").toList()) {
                const auto preview=value.toMap();
                QVERIFY(preview.value("time").toDouble()>=previous);
                previous=preview.value("time").toDouble();
                const auto url=preview.value("url").toString();
                QVERIFY2(!url.isEmpty(),"A character appearance has no preview");
                QVERIFY(QFileInfo::exists(QUrl(url).toLocalFile()));
                const auto [found,inserted]=frameUrls.emplace(preview.value("time").toDouble(),url);
                if(!inserted)QCOMPARE(url,found->second);
            }
        }
        QVERIFY(unresolved<=20);
        if(qEnvironmentVariableIsSet("PF_TEST_EXPECT_HIDDEN_UNRESOLVED")) {
            QCOMPARE(unresolved,20);
            QVERIFY(analysis->hiddenUnresolvedCharacters()>0);
        }
        QSaveFile file(report);QVERIFY(file.open(QIODevice::WriteOnly));
        const auto bytes=QJsonDocument::fromVariant(analysis->targetCandidates()).toJson(QJsonDocument::Compact);
        QCOMPARE(file.write(bytes),bytes.size());QVERIFY(file.commit());
        analysis->stopAnalysis();QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy(),10000);
    }
    void realVideoAllPeopleSearch() {
        const auto source=qEnvironmentVariable("PF_TEST_REAL_CHARACTER_VIDEO");
        const auto model=qEnvironmentVariable("PF_MODEL_PATH");
        const auto cache=qEnvironmentVariable("PF_TEST_REAL_CHARACTER_CACHE");
        if(source.isEmpty() || model.isEmpty() || cache.isEmpty()) QSKIP("Opt-in real all-people search");
        QVERIFY(QFileInfo::exists(source)); QVERIFY(QFileInfo::exists(model));
        pfservices::Settings settings; settings.modelPath=model.toStdString(); settings.provider="cuda";
        settings.modelChoice=QFileInfo(model).fileName().toStdString(); settings.qualityProfile="fast";
        settings.cachePath=cache.toStdString(); settings.processingThreads=2;
        std::string error; QVERIFY2(pfservices::SettingsStore().save(settings,error),error.c_str());
        pfui::AppInfo::registerQmlTypes(); auto* info=pfui::AppInfo::instance(); info->initializeBackendsAsync();
        QTRY_VERIFY_WITH_TIMEOUT(!info->backendInitializing(),30000);
        auto* analysis=pfui::AnalysisController::instance(); analysis->setProviderChoice("cuda");
        analysis->setAnalysisMode("combined");
        QElapsedTimer timer; timer.start();
        analysis->analyzeAllPeople({source});
        QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy() || analysis->awaitingTarget(),600000);
        QVERIFY2(!analysis->awaitingTarget(),"All-people search unexpectedly requested a character");
        QVERIFY2(analysis->analysisCompleted(),qPrintable(analysis->status()));
        qInfo() << "ALL_PEOPLE_REAL elapsed_ms=" << timer.elapsed() << "analyzed_frames=" << analysis->analyzedFrames()
                << "matches=" << analysis->matchCount() << "status=" << analysis->status();
        const auto report=qEnvironmentVariable("PF_TEST_REAL_CHARACTER_REPORT");
        if(!report.isEmpty()) {
            QSaveFile file(report); QVERIFY(file.open(QIODevice::WriteOnly));
            const auto data=QJsonDocument::fromVariant(analysis->results()).toJson(QJsonDocument::Compact);
            QCOMPARE(file.write(data),data.size()); QVERIFY(file.commit());
        }
    }
    void realVideoMergedCharacterSearch() {
        const auto source=qEnvironmentVariable("PF_TEST_REAL_CHARACTER_VIDEO");
        const auto model=qEnvironmentVariable("PF_MODEL_PATH");
        const auto cache=qEnvironmentVariable("PF_TEST_REAL_CHARACTER_CACHE");
        const auto windows=qEnvironmentVariable("PF_TEST_REAL_MERGED_WINDOWS");
        if(source.isEmpty() || model.isEmpty() || cache.isEmpty() || windows.isEmpty())
            QSKIP("Opt-in merged-character real-video search");
        const auto target=qEnvironmentVariable("PF_TEST_MERGED_TARGET_ID","189");
        const auto oldTarget=qEnvironmentVariable("PF_TEST_MERGED_OLD_ID","940").toUtf8();
        QSet<qint64> expectedTracks;
        for(const auto& id:qEnvironmentVariable("PF_TEST_MERGED_TRACKS","189,940,1062").split(',')) {
            bool ok=false;const auto track=id.toLongLong(&ok);QVERIFY(ok);expectedTracks.insert(track);
        }
        pfservices::Settings settings;settings.modelPath=model.toStdString();settings.provider="cuda";
        settings.modelChoice=QFileInfo(model).fileName().toStdString();settings.qualityProfile="fast";
        settings.cachePath=cache.toStdString();settings.processingThreads=2;
        std::string error;QVERIFY2(pfservices::SettingsStore().save(settings,error),error.c_str());
        qputenv("PF_DEBUG_POSES_JSON",windows.toUtf8());
        const auto cleanup=qScopeGuard([] {qunsetenv("PF_DEBUG_POSES_JSON");});
        pfui::AppInfo::registerQmlTypes();auto* info=pfui::AppInfo::instance();info->initializeBackendsAsync();
        QTRY_VERIFY_WITH_TIMEOUT(!info->backendInitializing(),30000);
        auto* analysis=pfui::AnalysisController::instance();analysis->setProviderChoice("cuda");
        analysis->setAnalysisMode("combined");analysis->analyzeFiles({source});
        QTRY_VERIFY_WITH_TIMEOUT(analysis->awaitingTarget() || !analysis->busy(),600000);
        QVERIFY2(analysis->awaitingTarget(),qPrintable(analysis->status()));
        bool found=false;
        for(const auto& row:analysis->targetCandidates()) {
            const auto candidate=row.toMap();
            if(candidate.value("id").toString()==target) {found=true;QCOMPARE(candidate.value("tracks").toInt(),static_cast<int>(expectedTracks.size()));}
        }
        QVERIFY(found);QVERIFY(analysis->selectTargetCharacter(target));
        QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy(),600000);
        QVERIFY2(analysis->analysisCompleted(),qPrintable(analysis->status()));
        QFile file(windows);QVERIFY(file.open(QIODevice::ReadOnly));
        const auto captured=QJsonDocument::fromJson(file.readAll()).array();
        QSet<qint64> tracks;
        for(const auto& row:captured)tracks.insert(row.toObject().value("track").toInteger());
        QCOMPARE(tracks,expectedTracks);
        // Simulate an explicit selection saved before the candidate was merged.
        // A warm rerun must map the old candidate ID to the merged candidate.
        pfservices::PfCache savedCache(std::filesystem::path(cache.toStdWString()));
        std::string selectionKey;
        for(const auto& key:savedCache.keysForSource(source.toStdString()))
            if(key.ends_with("|selected-target-v2"))selectionKey=key;
        QVERIFY(!selectionKey.empty());
        QVERIFY2(savedCache.putForSource(source.toStdString(),selectionKey,
            {oldTarget.begin(),oldTarget.end()},error),error.c_str());
        analysis->analyzeFiles({source});
        QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy() || analysis->awaitingTarget(),600000);
        QVERIFY2(!analysis->awaitingTarget(),"An earlier candidate ID was not mapped to the merged identity");
        QVERIFY2(analysis->analysisCompleted(),qPrintable(analysis->status()));
        QFile again(windows);QVERIFY(again.open(QIODevice::ReadOnly));
        const auto repeated=QJsonDocument::fromJson(again.readAll()).array();tracks.clear();
        for(const auto& row:repeated)tracks.insert(row.toObject().value("track").toInteger());
        QCOMPARE(tracks,expectedTracks);
    }
    void realSeriesIncrementalRestart() {
        const auto manifestPath=qEnvironmentVariable("PF_TEST_REAL_SERIES_MANIFEST");
        if(manifestPath.isEmpty())QSKIP("Opt-in real-source series/restart regression");
        QFile file(manifestPath);QVERIFY(file.open(QIODevice::ReadOnly));
        const auto manifest=QJsonDocument::fromJson(file.readAll()).object();
        const auto phase=qEnvironmentVariable("PF_TEST_REAL_SERIES_PHASE","seed");
        const bool manualPhase=phase=="manual"||phase=="manual-warm";
        QVERIFY(phase=="seed"||phase=="incremental"||phase=="warm"||manualPhase);
        QStringList sources;
        for(const auto value:manifest.value("initialSources").toArray())sources.push_back(value.toString());
        QCOMPARE(sources.size(),8);
        const auto initialSources=sources;
        if(phase!="seed")for(const auto value:manifest.value("addedSources").toArray())sources.push_back(value.toString());
        QCOMPARE(sources.size(),phase=="seed"?8:10);
        if(manualPhase)sources={manifest.value("minorityTargetRange").toObject().value("source").toString()};
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
        QString expectedManualId;
        if(manualPhase) {
            QFile prior(QFileInfo(manifestPath).dir().filePath(phase=="manual"?"incremental.json":"manual.json"));
            QVERIFY(prior.open(QIODevice::ReadOnly));
            const auto rows=QJsonDocument::fromJson(prior.readAll()).object().value("sources").toArray();
            for(const auto row:rows)if(row.toObject().value("source").toString()==sources.front())
                expectedManualId=row.toObject().value("targetCharacterId").toString();
            QVERIFY(!expectedManualId.isEmpty());
        }
        if(phase=="seed"||phase=="manual") {
            QVERIFY2(analysis->awaitingTarget(),qPrintable(analysis->status()));
            QCOMPARE(analysis->targetSelectionSource(),manualPhase?sources.front():manifest.value("anchor").toString());
            QVERIFY(analysis->selectTargetCharacter(manualPhase?expectedManualId:manifest.value("anchorCandidateId").toString()));
            QTRY_VERIFY_WITH_TIMEOUT(analysis->awaitingTarget()||!analysis->busy(),180000);
        }
        if(phase=="manual"&&!analysis->awaitingTarget()&&analysis->analysisCompleted()) {
            analysis->reselectTargetCharacter(sources.front());
            QTRY_VERIFY_WITH_TIMEOUT(analysis->awaitingTarget()||!analysis->busy(),180000);
            QVERIFY(analysis->awaitingTarget());
            QString alternative;double longest=3.1;
            for(const auto& value:analysis->targetCandidates()) {
                const auto row=value.toMap();
                if(row.value("id").toString()!=expectedManualId&&row.value("presence").toDouble()>longest) {
                    longest=row.value("presence").toDouble();alternative=row.value("id").toString();
                }
            }
            QVERIFY2(!alternative.isEmpty(),"Fixture must contain a longer-lived competing person");
            QVERIFY(analysis->selectTargetCharacter(alternative));expectedManualId=alternative;
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
            QCOMPARE(summary.value("targetAutomatic").toBool(),!manualPhase&&source!=manifest.value("anchor").toString());
            QVERIFY(summary.contains("observationCacheHit"));
            QCOMPARE(summary.value("observationCacheHit").toBool(),manualPhase||phase=="warm"||(phase=="incremental"&&initialSources.contains(source)));
            if(manualPhase) {
                QCOMPARE(summary.value("targetCharacterId").toString(),expectedManualId);
                QVERIFY(summary.value("targetPresenceSeconds").toDouble()>3.1);
            } else if(source==manifest.value("minorityTargetRange").toObject().value("source").toString()) {
                const auto presence=summary.value("targetPresenceSeconds").toDouble();
                QVERIFY(presence>1 && presence<=3.1);
                QVERIFY(presence/summary.value("durationSeconds").toDouble()<=.085);
            }
            auto row=QJsonObject::fromVariantMap(summary);row.insert("source",source);summaries.push_back(row);
        }
        report.insert("sources",summaries);
        if(phase=="warm"||manualPhase) {QCOMPARE(analysis->analyzedFrames(),0);QCOMPARE(analysis->poseDetectionCount(),0);}
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
        QQmlApplicationEngine engine;pfui::installResultPreviewImages(engine);engine.addImportPath("qrc:/qt/qml");engine.load(QUrl("qrc:/qt/qml/PfUi/qml/Main.qml"));
        QVERIFY(!engine.rootObjects().isEmpty());auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().front());QVERIFY(window);
        window->setProperty("sourceFiles",QVariantList{source});
        QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy(),10000);
        analysis->analyzeFiles({source});QTRY_VERIFY_WITH_TIMEOUT(analysis->awaitingTarget()||!analysis->busy(),90000);
        if(!analysis->awaitingTarget()&&analysis->analysisCompleted()) {
            analysis->reselectTargetCharacter(source);
            QTRY_VERIFY_WITH_TIMEOUT(analysis->awaitingTarget()||!analysis->busy(),90000);
        }
        QVERIFY2(analysis->awaitingTarget(),qPrintable(analysis->status()));
        double leadPresence=0;QString leadId;
        for(const auto& value:analysis->targetCandidates()) {
            const auto row=value.toMap();
            if(row.value("presence").toDouble()>leadPresence&&row.value("tracks").toInt()>30&&!row.value("unresolved").toBool()) {
                leadPresence=row.value("presence").toDouble();leadId=row.value("id").toString();
            }
        }
        QVERIFY(leadPresence>60);QVERIFY(!leadId.isEmpty());
        if(qEnvironmentVariableIsSet("PF_TEST_EXPECT_HIDDEN_UNRESOLVED")) {
            auto* selector=window->findChild<QQuickItem*>("targetCharacterPanel");QVERIFY(selector);
            selector->setProperty("showUnresolved",true);
            auto* notice=selector->findChild<QQuickItem*>("unresolvedLimitNotice");QVERIFY(notice);
            QTRY_VERIFY(notice->isVisible());
            const auto message=notice->property("text").toString();
            QVERIFY(message.contains("20"));
            QVERIFY(message.contains(QString::number(20+analysis->hiddenUnresolvedCharacters())));
        }
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
    void analysisRunsWithoutDiskCache() {
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto model=directory.filePath("workflow-pose.onnx");
        QFile file(model); QVERIFY(file.open(QIODevice::WriteOnly));
        const auto data=pftest::twoPeopleModel();
        QCOMPARE(file.write(data.data(),data.size()),qint64(data.size())); file.close();
        qputenv("PF_MODEL_PATH",model.toUtf8()); qputenv("PF_ALLOW_UNVERIFIED_TARGETS","1");
        const auto cleanup=qScopeGuard([] {qunsetenv("PF_MODEL_PATH");qunsetenv("PF_ALLOW_UNVERIFIED_TARGETS");});
        pfservices::Settings settings;settings.modelPath=model.toStdString();
        settings.modelChoice="workflow-pose.onnx";settings.provider="cpu";settings.qualityProfile="fast";
        settings.cachePath=directory.filePath("cache").toStdString();settings.processingThreads=1;
        std::string error; QVERIFY2(pfservices::SettingsStore().save(settings,error),error.c_str());
        pfui::AppInfo::registerQmlTypes(); auto* info=pfui::AppInfo::instance();info->initializeBackendsAsync();
        QTRY_VERIFY_WITH_TIMEOUT(!info->backendInitializing(),30000);
        auto* analysis=pfui::AnalysisController::instance();
        analysis->setCacheEnabled(true);
        analysis->setCacheEnabled(false);
        pfservices::SettingsStore::flushPendingWrites();
        const auto persisted=pfservices::SettingsStore().load(error);
        QVERIFY2(error.empty(),error.c_str()); QVERIFY(!persisted.cacheEnabled);
        const QString source=QStringLiteral(PF_TARGET_VIDEO);
        const QString second=directory.filePath("second-video.mp4"); QVERIFY(QFile::copy(source,second));
        for(int run=0;run<2;++run) {
            analysis->analyzeFiles({source,second});
            QTRY_VERIFY_WITH_TIMEOUT(analysis->awaitingTarget() || !analysis->busy(),30000);
            QVERIFY2(analysis->awaitingTarget(),qPrintable(analysis->status()));
            const auto rows=analysis->targetCandidates(); QVERIFY(!rows.isEmpty());
            const auto preparedFrames=analysis->analyzedFrames();
            if(run==0) QVERIFY(analysis->selectTargetCharacter(rows[0].toMap().value("id").toString()));
            else QVERIFY(analysis->continueWithoutTargetCharacter());
            QCOMPARE(analysis->analyzedFrames(),preparedFrames);
            QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy() || analysis->awaitingTarget(),30000);
            QVERIFY2(!analysis->awaitingTarget(),"Unexpected extra target selection");
            QVERIFY2(analysis->analysisCompleted(),qPrintable(analysis->status()));
            QVERIFY(analysis->analyzedFrames()>0);
            QVERIFY(!analysis->status().contains(QStringLiteral("Сохраняем кеш анализа")));
        }
        QVERIFY(!QDir(directory.filePath("cache")).exists());
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
        analysis->setCachePath(directory.filePath("cache"));
        analysis->setCacheEnabled(true);
        const QString source=QStringLiteral(PF_TARGET_VIDEO);
        QQmlApplicationEngine engine;pfui::installResultPreviewImages(engine);
        engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
        engine.load(QUrl(QStringLiteral("qrc:/qt/qml/PfUi/qml/Main.qml")));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().front());
        QVERIFY(window);
        auto* settingsDialog=window->findChild<QObject*>("settingsDialog"); QVERIFY(settingsDialog);
        QVERIFY(QMetaObject::invokeMethod(settingsDialog,"open"));
        QTRY_VERIFY_WITH_TIMEOUT(settingsDialog->findChild<QObject*>("createCacheCheck"),5000);
        auto* cacheCheck=settingsDialog->findChild<QObject*>("createCacheCheck");
        QTRY_VERIFY(cacheCheck->property("checked").toBool());
        analysis->setCacheEnabled(false); QTRY_VERIFY(!cacheCheck->property("checked").toBool());
        analysis->setCacheEnabled(true); QTRY_VERIFY(cacheCheck->property("checked").toBool());
        QVERIFY(QMetaObject::invokeMethod(settingsDialog,"close"));
        window->setProperty("sourceFiles",QVariantList{source});
        QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy(),10000);
        qunsetenv("PF_ALLOW_UNVERIFIED_TARGETS");
        const auto previousPoseDetections=analysis->poseDetectionCount();
        analysis->analyzeFiles({source});QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy(),10000);
        QVERIFY(!analysis->awaitingTarget());QVERIFY(!analysis->analysisCompleted());
        QVERIFY(analysis->status().contains(QStringLiteral("распознавание персонажей")));
        QCOMPARE(analysis->analyzedFrames(),0);QCOMPARE(analysis->poseDetectionCount(),previousPoseDetections);
        qputenv("PF_ALLOW_UNVERIFIED_TARGETS","1");
        analysis->analyzeFiles({source});
        QTRY_VERIFY_WITH_TIMEOUT(analysis->awaitingTarget() || !analysis->busy(),30000);
        QVERIFY2(analysis->awaitingTarget(),qPrintable(analysis->status()));
        auto* selector=window->findChild<QQuickItem*>("targetCharacterPanel");
        QVERIFY(selector && selector->isVisible());
        auto* allPeople=selector->findChild<QQuickItem*>("analyzeAllPeopleButton");
        auto* confirm=selector->findChild<QQuickItem*>("confirmCharacterButton");
        QVERIFY(allPeople && confirm && allPeople->isEnabled());
        QVERIFY(allPeople->mapToScene(QPointF()).y() >= confirm->mapToScene(QPointF(0,confirm->height())).y());
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
        const auto originalSummary=analysis->summaryForSource(source);
        analysis->analyzeFiles({source});
        QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy(),30000);
        QVERIFY(analysis->analysisCompleted());
        QCOMPARE(analysis->analyzedFrames(),0);
        QCOMPARE(analysis->poseDetectionCount(),0);
        analysis->setAnalysisMode("static");
        analysis->analyzeFiles({source});
        QTRY_VERIFY2_WITH_TIMEOUT(!analysis->busy(),qPrintable(analysis->status()+" | "+analysis->progressStage()+" | "+
            QString::fromUtf8(QJsonDocument::fromVariant(analysis->summaryForSource(source)).toJson(QJsonDocument::Compact))),30000);
        QVERIFY(analysis->analysisCompleted());
        QCOMPARE(analysis->analyzedFrames(),0);
        analysis->setAnalysisMode("motion");
        analysis->reselectTargetCharacter(QDir::toNativeSeparators(source));
        QTRY_VERIFY_WITH_TIMEOUT(analysis->awaitingTarget() || !analysis->busy(),30000);
        QVERIFY2(analysis->awaitingTarget(),qPrintable(analysis->status()));
        QVERIFY(analysis->selectTargetCharacter(second));
        QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy(),30000);
        QCOMPARE(analysis->summaryForSource(source).value("targetCharacterId").toString(),second);
        QVERIFY2(analysis->analyzedFrames()==0,qPrintable(QString::fromUtf8(QJsonDocument::fromVariant(QVariantMap{
            {"original",originalSummary},{"reselected",analysis->summaryForSource(source)}}).toJson(QJsonDocument::Compact))));
        analysis->reselectTargetCharacter(source);
        QTRY_VERIFY_WITH_TIMEOUT(analysis->awaitingTarget() || !analysis->busy(),30000);
        QVERIFY(analysis->awaitingTarget());analysis->stopAnalysis();
        QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy(),5000);
        QVERIFY(!analysis->awaitingTarget());
        QVERIFY(!analysis->analysisCompleted());
        // A batch uses the saved single anchor and never prompts again for
        // another source, even when its identity evidence is insufficient.
        const auto episode=directory.filePath("episode-two.mp4");
        QVERIFY(QFile::copy(source,episode));
        analysis->analyzeFiles({source,episode});
        QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy() || analysis->awaitingTarget(),30000);
        QVERIFY2(!analysis->awaitingTarget(),"A second source requested another character");
        QVERIFY2(analysis->analysisCompleted(),qPrintable(analysis->status()));
        QCOMPARE(analysis->summaryForSource(source).value("targetCharacterId").toString(),second);
        QVERIFY(analysis->summaryForSource(episode).value("targetCharacterId").toString().isEmpty());
        QVERIFY(analysis->summaryForSource(episode).value("targetMatchUncertain").toBool());
        QVERIFY(analysis->status().contains(QStringLiteral("Персонаж не подтверждён")));
        // Explicitly choose once for the whole batch; the other video must not
        // open a second selector, even without reliable identity evidence.
        analysis->reselectTargetCharacter(source);
        QTRY_VERIFY_WITH_TIMEOUT(analysis->awaitingTarget() || !analysis->busy(),30000);
        QVERIFY2(analysis->awaitingTarget(),qPrintable(analysis->status()));
        QVERIFY(analysis->selectTargetCharacter(first));
        QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy() || analysis->awaitingTarget(),30000);
        QVERIFY2(!analysis->awaitingTarget(),"Batch requested another character");
        QVERIFY2(analysis->analysisCompleted(),qPrintable(analysis->status()));
        QCOMPARE(analysis->summaryForSource(source).value("targetCharacterId").toString(),first);
        QVERIFY(analysis->summaryForSource(episode).value("targetMatchUncertain").toBool());
        // A saved character choice must not force a target in all-people mode.
        // This mode also works when optional face/ReID models are unavailable.
        qunsetenv("PF_ALLOW_UNVERIFIED_TARGETS");
        analysis->analyzeAllPeople({source,episode});
        QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy(),30000);
        QVERIFY2(analysis->analysisCompleted(),qPrintable(analysis->status()));
        QVERIFY(!analysis->awaitingTarget());
        QVERIFY(analysis->summaryForSource(source).value("targetCharacterId").toString().isEmpty());
        QVERIFY(analysis->summaryForSource(episode).value("targetCharacterId").toString().isEmpty());
        qputenv("PF_ALLOW_UNVERIFIED_TARGETS","1");
        analysis->analyzeFiles({source,episode});
        QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy(),30000);
        QVERIFY(analysis->analysisCompleted());
        QCOMPARE(analysis->analyzedFrames(),0);
        QVERIFY(analysis->summaryForSource(episode).value("targetCharacterId").toString().isEmpty());
        QVERIFY(analysis->summaryForSource(episode).value("targetMatchUncertain").toBool());
        pfservices::SettingsStore::flushPendingWrites();
        struct DelayedWrite {
            std::mutex mutex;std::condition_variable ready;bool released=false;
            std::atomic_bool entered=false;
        };
        auto delayed=std::make_shared<DelayedWrite>();
        pfservices::SettingsStore().updateAsync("test-delay",[delayed](auto&) {
            std::unique_lock lock(delayed->mutex);delayed->entered=true;
            delayed->ready.wait(lock,[&]{return delayed->released;});
        });
        const auto release=[delayed] {
            {std::lock_guard lock(delayed->mutex);delayed->released=true;}
            delayed->ready.notify_all();
        };
        const auto releaseGuard=qScopeGuard(release);
        QTRY_VERIFY_WITH_TIMEOUT(delayed->entered.load(),5000);
        const auto previousDebug=qgetenv("PF_DEBUG_CACHE");
        const auto debugGuard=qScopeGuard([previousDebug] {
            if(previousDebug.isNull())qunsetenv("PF_DEBUG_CACHE");else qputenv("PF_DEBUG_CACHE",previousDebug);
        });
        qputenv("PF_DEBUG_CACHE","buffer");
        const auto persistedCache=analysis->cachePath();
        const auto newCache=directory.filePath("pending-settings-cache");
        const auto newThreshold=analysis->sceneThreshold()+1;
        analysis->setCachePath(newCache);analysis->setSceneThreshold(newThreshold);
        analysis->analyzeFiles({source});
        QTRY_VERIFY_WITH_TIMEOUT(analysis->awaitingTarget() || !analysis->busy(),30000);
        if(analysis->awaitingTarget())QVERIFY(analysis->selectTargetCharacter(first));
        QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy(),30000);
        QVERIFY(analysis->analysisCompleted());
        const auto decision=analysis->summaryForSource(source).value("observationCacheDecision").toMap();
        QCOMPARE(decision.value("persistedRoot").toString(),persistedCache);
        QCOMPARE(decision.value("configuredRoot").toString(),newCache);
        QCOMPARE(decision.value("sceneThreshold").toDouble(),newThreshold);
        QVERIFY(analysis->analyzedFrames()>0);
        release();pfservices::SettingsStore::flushPendingWrites();
        analysis->analyzeFiles({source});
        QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy(),30000);
        QVERIFY(analysis->analysisCompleted());QCOMPARE(analysis->analyzedFrames(),0);
    }
};

int main(int argc,char** argv) {
    pfui::configureUiRuntime();pfui::GuiApartment guiApartment;QGuiApplication app(argc,argv);
    QCoreApplication::setOrganizationName("ParallelFinderTests");
    QTemporaryDir profile;
    if(!profile.isValid())return 2;
    // Unique app profile keeps the test independent of installed/user settings.
    QCoreApplication::setApplicationName("TargetCharacter-"+QFileInfo(profile.path()).fileName());
    TargetCharacterTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "test_target_character.moc"
