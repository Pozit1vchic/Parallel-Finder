#include <QtTest/QTest>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QElapsedTimer>
#include <HeadOrientationReview.h>
#include <ResultClassification.h>

class HeadReviewTests:public QObject {
    Q_OBJECT
private slots:
    void realSourceColdWarmAndCancellation() {
        const auto root=qEnvironmentVariable("PF_HEAD_REVIEW_ROOT");
        const auto model=qEnvironmentVariable("PF_HEAD_REVIEW_MODEL");
        const auto detector=qEnvironmentVariable("PF_HEAD_REVIEW_DETECTOR");
        const auto casePrefix=qEnvironmentVariable("PF_HEAD_REVIEW_CASE_PREFIX");
        if(root.isEmpty() || model.isEmpty() || detector.isEmpty())QSKIP("Opt-in real-source head-orientation review");
        const auto read=[](const QString& path) {
            QFile file(path);if(!file.open(QIODevice::ReadOnly))return QJsonObject{};
            return QJsonDocument::fromJson(file.readAll()).object();
        };
        const auto cases=read(root+"/independent-motion-reviewed.json")["cases"].toArray();
        QVERIFY(!cases.empty());
        std::map<QString,QJsonObject> rows;
        for(int video=1;video<=3;++video)
            for(const auto value:read(root+QString("/independent-%1-final-compound.json").arg(video))["results"].toArray()) {
                const auto row=value.toObject();rows.emplace(row["caseId"].toString(),row);
            }
        std::vector<pfcore::MotionWindow> windows;std::vector<pfcore::MotionMatch> matches;
        std::map<std::string,std::string> namespaces;QStringList identifiers;
        for(const auto value:cases) {
            const auto item=value.toObject();const auto status=item["status"].toString();
            if(status=="cut-crossing-excluded" || status=="development-overlap-excluded")continue;
            const auto id=item["caseId"].toString();
            if(!casePrefix.isEmpty() && !id.startsWith(casePrefix))continue;
            const auto row=rows.at(id);
            const auto source=read(root+"/native-"+id+"-cuts.json")["source"].toString();
            QVERIFY2(QFile::exists(source),qPrintable(source));
            pfcore::MotionWindow w;w.sourceId=source.toStdString();
            w.sceneStartSeconds=item["start"].toDouble()-.001;
            w.sceneEndSeconds=item["end"].toDouble()+.001;
            for(const auto fv:row["leftFrames"].toArray()) {
                const auto f=fv.toObject();pfcore::PoseFrame frame;frame.timestampSeconds=f["time"].toDouble();
                for(const auto pv:f["points"].toArray()) {
                    const auto point=pv.toArray();frame.keypoints.push_back({point[0].toDouble(),point[1].toDouble(),point[2].toDouble()});
                }
                w.frames.push_back(std::move(frame));
            }
            QVERIFY(w.frames.size()>=5);
            pfcore::MotionMatch match;match.leftIndex=match.rightIndex=windows.size();
            match.leftSourceId=match.rightSourceId=w.sourceId;
            match.leftStartSeconds=match.rightStartSeconds=w.frames.front().timestampSeconds;
            match.leftEndSeconds=match.rightEndSeconds=w.frames.back().timestampSeconds;
            match.headOnlyComparison=true;matches.push_back(match);identifiers.push_back(id);
            namespaces.emplace(w.sourceId,"fixed-read-only-test-input-v1");windows.push_back(std::move(w));
        }
        QVERIFY2(!windows.empty(),"Explicit source selection contains no usable cases");
        QTemporaryDir cache;QVERIFY(cache.isValid());
        const auto originalWindows=windows;
        const auto provider=qEnvironmentVariable("PF_HEAD_REVIEW_PROVIDER")=="cuda" ? pfgpu::Provider::Cuda:pfgpu::Provider::Cpu;
        pfcore::VideoDecodeOptions options;options.threads=2;
        QElapsedTimer clock;clock.start();
        const auto invoke=[&](const std::function<bool()>& cancelled,const std::function<void(std::size_t,std::size_t)>& progress) {
            return pfui::reviewMatchedHeadOrientations(windows,matches,model.toStdString(),detector.toStdString(),
                cache.path().toStdString(),namespaces,provider,options,cancelled,progress);
        };
        const auto cold=invoke([]{return false;},{});
        QVERIFY2(cold.error.empty(),cold.error.c_str());QCOMPARE(cold.cached,0U);
        if(casePrefix.isEmpty()) {QVERIFY(cold.requested>100);QVERIFY(cold.observed>50);}
        else {QVERIFY(cold.requested>0);QVERIFY(cold.observed>0);}
        QVERIFY(cold.decoded>0);const auto elapsed=clock.elapsed();
        QVERIFY(cold.preparationMicros>0);
        QVERIFY(cold.cacheMicros>0);
        QVERIFY(cold.decodeMicros>0);
        QVERIFY(cold.faceMicros>0);
        const auto saved=windows;
        QJsonArray report;
        for(std::size_t i=0;i<windows.size();++i) {
            const auto& w=windows[i];QJsonArray samples;
            // New head measurements must not rewrite the old motion cache's
            // source, scene boundaries or pose coordinates.
            const auto& original=originalWindows[i];
            QCOMPARE(w.sourceId,original.sourceId);
            QCOMPARE(w.sceneStartSeconds,original.sceneStartSeconds);
            QCOMPARE(w.sceneEndSeconds,original.sceneEndSeconds);
            QCOMPARE(w.frames.size(),original.frames.size());
            for(std::size_t fi=0;fi<w.frames.size();++fi) {
                QCOMPARE(w.frames[fi].timestampSeconds,original.frames[fi].timestampSeconds);
                QCOMPARE(w.frames[fi].keypoints.size(),original.frames[fi].keypoints.size());
                for(std::size_t pi=0;pi<w.frames[fi].keypoints.size();++pi) {
                    const auto& a=w.frames[fi].keypoints[pi];const auto& b=original.frames[fi].keypoints[pi];
                    QCOMPARE(a.x,b.x);QCOMPARE(a.y,b.y);QCOMPARE(a.confidence,b.confidence);
                }
            }
            QCOMPARE(w.headOrientations.size(),w.frames.size());
            for(const auto& s:w.headOrientations) {
                QJsonArray rotation;for(int r=0;r<3;++r)rotation.append(QJsonArray{s.rotation[r*3],s.rotation[r*3+1],s.rotation[r*3+2]});
                samples.append(QJsonObject{{"time",s.time},{"accepted",s.observed},{"rotation",rotation}});
            }
            auto m=matches[i];QVariantMap row;
            const pfui::ResultPoseEvidence evidence{pfui::groupingEvidence(w),pfui::groupingEvidence(w)};
            pfui::classifyResultRange(row,m,evidence);
            auto bodyMatch=m;bodyMatch.headOnlyComparison=false;QVariantMap bodyRow;
            pfui::classifyResultRange(bodyRow,bodyMatch,evidence);
            report.append(QJsonObject{{"caseId",identifiers[i]},{"gesture",QString::fromStdString(m.gestureLabel)},
                {"classification",row.value("classification").toString()},
                {"bodyClassification",bodyRow.value("classification").toString()},
                {"bodyClassificationPartial",bodyRow.value("classificationPartial").toBool()},
                {"samples",samples}});
        }
        const auto warm=invoke([]{return false;},{});
        QVERIFY2(warm.error.empty(),warm.error.c_str());QCOMPARE(warm.cached,cold.requested);
        QCOMPARE(warm.decoded,0U);QCOMPARE(warm.observed,cold.observed);
        QVERIFY(warm.cacheMicros>0);
        QCOMPARE(warm.decodeMicros,0ULL);QCOMPARE(warm.faceMicros,0ULL);
        QCOMPARE(warm.orientationMicros,0ULL);
        for(std::size_t wi=0;wi<windows.size();++wi)for(std::size_t si=0;si<windows[wi].headOrientations.size();++si) {
            const auto& a=windows[wi].headOrientations[si];const auto& b=saved[wi].headOrientations[si];
            QCOMPARE(a.time,b.time);QCOMPARE(a.observed,b.observed);QVERIFY(a.rotation==b.rotation);
        }
        for(auto& [source,space]:namespaces)space+="-new-generation";
        bool cancel=false;
        const auto partial=invoke([&]{return cancel;},[&](std::size_t done,std::size_t){if(done==4)cancel=true;});
        QVERIFY(partial.error.empty());QVERIFY(cancel);QCOMPARE(partial.cached,0U);
        const auto output=qEnvironmentVariable("PF_HEAD_REVIEW_REPORT");
        if(!output.isEmpty()) {
            QFile file(output);QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::NewOnly));
            file.write(QJsonDocument(QJsonObject{{"results",report},{"coldMilliseconds",elapsed},
                {"casePrefix",casePrefix},{"scope","explicit development-source subset; cache/producer integration, not identity or accuracy"},
                {"requested",static_cast<qint64>(cold.requested)},{"observed",static_cast<qint64>(cold.observed)},
                {"warmDecoded",0},{"professionalMovementAccuracyEstablished",false}}).toJson());
        }
    }
};
QTEST_GUILESS_MAIN(HeadReviewTests)
#include "test_head_review.moc"
