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
#include <QtTest/QTest>
#include "../helpers/onnx_two_people.hpp"

class TargetCharacterTests : public QObject {
    Q_OBJECT
private slots:
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
        analysis->analyzeFiles({source});
        QTRY_VERIFY_WITH_TIMEOUT(analysis->awaitingTarget() || !analysis->busy(),30000);
        QVERIFY2(analysis->awaitingTarget(),qPrintable(analysis->status()));
        QTest::qWait(300);
        const auto image=window->grabWindow();
        QVERIFY(!image.isNull());
        QVERIFY(image.save(QCoreApplication::applicationDirPath()+"/target-character-panel.png"));
        auto* selector=window->findChild<QQuickItem*>("targetCharacterPanel");
        QVERIFY(selector && selector->isVisible());
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
        analysis->reselectTargetCharacter(source);
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
