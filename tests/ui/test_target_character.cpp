#include <AnalysisController.h>
#include <AppInfo.h>
#include <UiRuntime.h>
#include <pfservices/SettingsStore.hpp>
#include <QGuiApplication>
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
        analysis->analyzeFiles({source});
        QTRY_VERIFY_WITH_TIMEOUT(analysis->awaitingTarget() || !analysis->busy(),30000);
        QVERIFY2(analysis->awaitingTarget(),qPrintable(analysis->status()));
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
