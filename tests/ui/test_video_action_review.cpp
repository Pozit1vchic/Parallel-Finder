#include <QtTest/QTest>
#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <VideoActionReview.h>
#include <ResultClassification.h>

class VideoActionReviewTests:public QObject {
    Q_OBJECT
private slots:
    void realWalkingColdWarmGenerationCutsAndCancellation() {
        const auto root=qEnvironmentVariable("PF_ACTION_REVIEW_ROOT");
        const auto model=qEnvironmentVariable("PF_ACTION_REVIEW_MODEL");
        const auto poseModel=qEnvironmentVariable("PF_ACTION_REVIEW_POSE");
        if(root.isEmpty() || model.isEmpty() || poseModel.isEmpty())QSKIP("Opt-in actual-source video-action review");
        QFile source(root+"/walking-windows.json");QVERIFY(source.open(QIODevice::ReadOnly));
        const auto rows=QJsonDocument::fromJson(source.readAll()).array();QVERIFY(rows.size()>3);
        std::vector<pfcore::MotionWindow> original;
        for(const int index:{1,3}) {
            const auto row=rows[index].toObject();pfcore::MotionWindow window;
            window.sourceId=row["source"].toString().toStdString();window.trackId=row["track"].toInteger();
            window.hasSceneIndex=true;window.sceneIndex=row["scene"].toInteger();
            window.sceneStartSeconds=row["sceneStart"].toDouble();window.sceneEndSeconds=row["sceneEnd"].toDouble();
            for(const auto fv:row["frames"].toArray()) {
                const auto f=fv.toObject();pfcore::PoseFrame frame;frame.timestampSeconds=f["time"].toDouble();
                for(const auto pv:f["points"].toArray()) {
                    const auto p=pv.toArray();frame.keypoints.push_back({p[0].toDouble(),p[1].toDouble(),p[2].toDouble()});
                }
                window.frames.push_back(std::move(frame));
            }
            QVERIFY(QFile::exists(QString::fromStdString(window.sourceId)));original.push_back(std::move(window));
        }
        pfcore::MotionMatch match;match.leftIndex=0;match.rightIndex=1;match.appearanceVerified=true;
        match.leftSourceId=original[0].sourceId;match.rightSourceId=original[1].sourceId;
        match.leftStartSeconds=5.839166666666666;match.leftEndSeconds=7.3406666666666665;
        match.rightStartSeconds=1652.025375;match.rightEndSeconds=1653.6937083333332;
        const std::vector matches{match};
        const auto provider=qEnvironmentVariable("PF_ACTION_REVIEW_PROVIDER")=="cuda" ? pfgpu::Provider::Cuda:pfgpu::Provider::Cpu;
        pfgpu::PoseEstimatorParams poseParams;poseParams.provider=provider;poseParams.intraOpThreads=4;
        pfgpu::PoseEstimator pose(poseModel.toStdString(),poseParams);
        QTemporaryDir directory;QVERIFY(directory.isValid());
        const auto cache=std::filesystem::path((directory.path()+"/actions").toStdString());
        QFile expensive(directory.path()+"/expensive-pose-cache.pfc");QVERIFY(expensive.open(QIODevice::WriteOnly));
        const QByteArray sentinel="forty-minute-analysis-cache-sentinel";QCOMPARE(expensive.write(sentinel),sentinel.size());expensive.close();
        std::map<std::string,std::string> namespaces;
        for(const auto& window:original)namespaces[window.sourceId]="immutable-test-source-generation-v1";
        pfcore::VideoDecodeOptions options;options.threads=2;
        const auto review=[&](auto& windows,const auto& requests,const auto& generation,const auto& cancelled,const auto& progress) {
            return pfui::reviewMatchedVideoActions(windows,requests,pose,poseModel.toStdString(),model.toStdString(),cache,
                generation,provider,options,cancelled,progress);
        };
        auto windows=original;
        const auto cold=review(windows,matches,namespaces,[]{return false;},[](auto,auto){});
        QVERIFY2(cold.error.empty(),cold.error.c_str());QCOMPARE(cold.requested,2U);QCOMPARE(cold.observed,2U);
        QCOMPARE(cold.cached,0U);QVERIFY(cold.decoded>20);QCOMPARE(cold.cuts,0U);QCOMPARE(cold.unassociated,0U);
        QVariantMap record;auto classified=match;
        pfui::classifyResultRange(record,classified,{pfui::groupingEvidence(windows[0]),pfui::groupingEvidence(windows[1])});
        QCOMPARE(record["classification"].toString().section('/',0,0),QString("step"));
        record["classificationManual"]=true;record["classification"]="head_turn";
        pfui::classifyResultRange(record,classified,{pfui::groupingEvidence(windows[0]),pfui::groupingEvidence(windows[1])});
        QCOMPARE(record["classification"].toString(),QString("head_turn"));
        const auto evidence=windows[0].videoActions[0];windows=original;
        const auto warm=review(windows,matches,namespaces,[]{return false;},[](auto,auto){});
        QVERIFY2(warm.error.empty(),warm.error.c_str());QCOMPARE(warm.cached,2U);QCOMPARE(warm.decoded,0U);
        QCOMPARE(windows[0].videoActions[0].scores,evidence.scores);
        QCOMPARE(windows[0].frames.size(),original[0].frames.size());
        QCOMPARE(windows[0].frames[0].keypoints[0].x,original[0].frames[0].keypoints[0].x);
        auto next=namespaces;for(auto& [_,value]:next)value+="-changed";
        windows=original;const auto refreshed=review(windows,matches,next,[]{return false;},[](auto,auto){});
        QVERIFY2(refreshed.error.empty(),refreshed.error.c_str());QCOMPARE(refreshed.cached,0U);QCOMPARE(refreshed.observed,2U);
        windows=original;bool cancelled=false;const auto cancel=review(windows,matches,namespaces,[&]{return cancelled;},
            [&](auto done,auto){if(done==1)cancelled=true;});
        QVERIFY(cancel.error.empty());QVERIFY(cancelled);
        QVERIFY(windows[0].videoActions.empty());QVERIFY(windows[1].videoActions.empty());
        windows={original[0]};windows[0].sceneEndSeconds=10;
        // Simulate an old mixed-shot pose window. These fabricated tail
        // samples are not actor evidence; the actual native cut must veto it.
        for(double time=7.8;time<8.91;time+=.2) {
            auto tail=windows[0].frames.back();tail.timestampSeconds=time;windows[0].frames.push_back(std::move(tail));
        }
        auto crossing=match;crossing.leftIndex=crossing.rightIndex=0;
        crossing.leftStartSeconds=crossing.rightStartSeconds=6.9;
        crossing.leftEndSeconds=crossing.rightEndSeconds=8.9;
        const auto cut=review(windows,std::vector{crossing},namespaces,[]{return false;},[](auto,auto){});
        QVERIFY2(cut.error.empty(),cut.error.c_str());QCOMPARE(cut.requested,1U);QCOMPARE(cut.cuts,1U);QCOMPARE(cut.observed,0U);
        QVERIFY(expensive.open(QIODevice::ReadOnly));QCOMPARE(expensive.readAll(),sentinel);
    }
};
QTEST_GUILESS_MAIN(VideoActionReviewTests)
#include "test_video_action_review.moc"
