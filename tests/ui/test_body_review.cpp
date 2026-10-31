#include <QtTest/QTest>
#include <BodyOrientationCache.h>
#include <BodyOrientationReview.h>
#include <pfcore/PathEncoding.hpp>
#include <pfcore/SceneDescriptor.hpp>
#include <ScenePresentation.h>
#include <ResultClassification.h>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonArray>
#include <QCryptographicHash>
#include <algorithm>

class BodyReviewTests:public QObject {
    Q_OBJECT
private slots:
    void realDifficultViewsKeepSubjectOwnership() {
        const auto model=qEnvironmentVariable("PF_BODY_REVIEW_MODEL"),poseModel=qEnvironmentVariable("PF_BODY_REVIEW_POSE_MODEL");
        if(model.isEmpty()||poseModel.isEmpty()||qEnvironmentVariable("PF_BODY_REVIEW_VIDEO").isEmpty()
            ||qEnvironmentVariable("PF_BODY_REVIEW_KYLE").isEmpty())QSKIP("Opt-in independent temporal orientation controls");
        QFile labels(QFINDTESTDATA("../fixtures/body-yaw-temporal-controls.json"));QVERIFY(labels.open(QIODevice::ReadOnly));
        const auto cases=QJsonDocument::fromJson(labels.readAll()).object()["cases"].toArray();QCOMPARE(cases.size(),4);
        pfgpu::PoseEstimatorParams params;params.provider=qEnvironmentVariable("PF_BODY_REVIEW_PROVIDER")=="cuda"?pfgpu::Provider::Cuda:pfgpu::Provider::Cpu;
        params.intraOpThreads=4;pfgpu::PoseEstimator pose(poseModel.toStdString(),params);
        std::map<QString,QString> sourceHashes;QJsonArray results;int totalWrong=0;
        QTemporaryDir cache;QVERIFY(cache.isValid());
        for(const auto value:cases) {
            const auto item=value.toObject();const auto id=item["id"].toString();
            const auto source=qEnvironmentVariable(item["sourceEnv"].toString().toUtf8().constData());
            if(!sourceHashes.contains(source)) {
                QFile file(source);QVERIFY(file.open(QIODevice::ReadOnly));QCryptographicHash hash(QCryptographicHash::Sha256);QVERIFY(hash.addData(&file));
                sourceHashes[source]=hash.result().toHex();
            }
            QCOMPARE(sourceHashes.at(source),item["sourceSha256"].toString());
            const auto times=item["times"].toArray(),box=item["targetBox"].toArray(),interval=item["yawInterval"].toArray();
            const bool nvidia=qEnvironmentVariable("PF_BODY_REVIEW_DECODE")=="nvdec";
            pfcore::VideoDecodeOptions options;options.threads=nvidia?8:4;options.maxWidth=1280;options.maxHeight=720;
            options.preferNvidia=nvidia;options.autoNvidiaConversion=nvidia;
            pfcore::VideoDecoder decoder;decoder.open(source.toStdString(),options);decoder.setRgbaMaxDimensions(1280,720);
            pfcore::MotionWindow w;w.sourceId=source.toStdString();w.sceneStartSeconds=times.first().toDouble();w.sceneEndSeconds=item["end"].toDouble();
            for(const auto requested:times) {
                pfcore::DecodedFrame frame;decoder.seek(requested.toDouble());
                do {QVERIFY(decoder.readNext(frame,false));}while(frame.timestampSeconds+1e-6<requested.toDouble());
                QVERIFY(decoder.convertCurrentFrameToRgba(frame));
                const auto people=pose.infer({frame.width,frame.height,frame.rgba.data()});
                std::vector<std::pair<double,std::size_t>> candidates;
                for(std::size_t index=0;index<people.size();++index) {
                    const auto& p=people[index];const double l=box[0].toDouble()*frame.width,t=box[1].toDouble()*frame.height,
                        r=box[2].toDouble()*frame.width,b=box[3].toDouble()*frame.height;
                    const double intersection=std::max(0.,std::min<double>(p.right,r)-std::max<double>(p.left,l))
                        *std::max(0.,std::min<double>(p.bottom,b)-std::max<double>(p.top,t));
                    const double area=(p.right-p.left)*(p.bottom-p.top)+(r-l)*(b-t)-intersection;
                    if(area>0)candidates.emplace_back(intersection/area,index);
                }
                std::sort(candidates.rbegin(),candidates.rend());
                QVERIFY2(!candidates.empty()&&candidates[0].first>=.35,qPrintable(id+": target pose missing"));
                QVERIFY2(candidates.size()==1||candidates[0].first-candidates[1].first>=.15,qPrintable(id+": target rectangle ambiguous"));
                const auto& p=people[candidates[0].second];pfcore::PoseFrame observed;observed.timestampSeconds=frame.timestampSeconds;
                for(std::size_t joint=0;joint+2<p.keypoints.size();joint+=3)observed.keypoints.push_back({p.keypoints[joint],p.keypoints[joint+1],p.keypoints[joint+2]});
                qInfo("BODY_POSE case=%s time=%.6f shoulders=%.3f,%.3f elbows=%.3f,%.3f hips=%.3f,%.3f",qPrintable(id),frame.timestampSeconds,
                    p.keypoints[5*3+2],p.keypoints[6*3+2],p.keypoints[7*3+2],p.keypoints[8*3+2],p.keypoints[11*3+2],p.keypoints[12*3+2]);
                w.frames.push_back(observed);
            }
            if(nvidia)QCOMPARE(QString::fromStdString(decoder.diagnostics().backend),QStringLiteral("nvdec"));
            std::vector<pfcore::MotionWindow> windows{w};pfcore::MotionMatch match;
            match.leftStartSeconds=match.rightStartSeconds=w.sceneStartSeconds;match.leftEndSeconds=match.rightEndSeconds=w.sceneEndSeconds;
            const auto stats=pfui::reviewMatchedBodyOrientations(windows,{match},pose,pfcore::pathFromUtf8(model.toStdString()),
                pfcore::pathFromUtf8(cache.path().toStdString()),{{w.sourceId,id.toStdString()}},params.provider,options,[]{return false;});
            QVERIFY2(stats.error.empty(),stats.error.c_str());QCOMPARE(stats.requested,std::size_t(3));
            QCOMPARE(stats.rawReplays,std::size_t(3));
            QJsonArray samples;int correct=0,wrong=0,unknown=0;
            for(const auto& s:windows[0].bodyOrientations) {
                double yaw=s.yawDegrees;if(interval[1].toDouble()>180&&yaw<0)yaw+=360;
                const bool agrees=s.observed&&yaw>=interval[0].toDouble()&&yaw<=interval[1].toDouble();
                if(!s.observed)++unknown;else if(agrees)++correct;else ++wrong;
                samples.push_back(QJsonObject{{"time",s.time},{"observed",s.observed},{"yaw",s.observed?QJsonValue(s.yawDegrees):QJsonValue::Null},
                    {"reason",QString::fromStdString(s.reason)},{"coarseCorrect",agrees},{"iou",s.associationIou},{"margin",s.associationMargin},
                    {"mirror",s.mirrorDifference},{"crop",s.cropDifference}});
            }
            const auto descriptor=pfcore::describeScene(windows[0],w.sceneStartSeconds,w.sceneEndSeconds);
            results.push_back(QJsonObject{{"id",id},{"samples",samples},{"correct",correct},{"wrong",wrong},{"unknown",unknown},
                {"decodeBackend",QString::fromStdString(decoder.diagnostics().backend)},
                {"conversionThreads",decoder.diagnostics().conversionThreads},
                {"rawReplays",static_cast<int>(stats.rawReplays)},
                {"descriptorYaw",descriptor.body.yawDegrees?QJsonValue(*descriptor.body.yawDegrees):QJsonValue::Null}});
            const auto coldObservations=windows[0].bodyOrientations;
            const auto warm=pfui::reviewMatchedBodyOrientations(windows,{match},pose,pfcore::pathFromUtf8(model.toStdString()),
                pfcore::pathFromUtf8(cache.path().toStdString()),{{w.sourceId,id.toStdString()}},params.provider,options,[]{return false;});
            QVERIFY2(warm.error.empty(),warm.error.c_str());QCOMPARE(warm.cached,std::size_t(3));
            QCOMPARE(warm.decoded,std::size_t(0));QCOMPARE(warm.poseInferences,std::size_t(0));QCOMPARE(warm.bodyInferences,std::size_t(0));
            QVERIFY(windows[0].bodyOrientations==coldObservations);
            qInfo("BODY_TEMPORAL case=%s correct=%d wrong=%d unknown=%d interval_observed=%d",qPrintable(id),correct,wrong,unknown,int(descriptor.body.yawDegrees.has_value()));
            QVERIFY2(correct>=item["minimumSupportedSamples"].toInt(),qPrintable(id+": measured positive orientation support regressed"));
            // Unknown on a visible view remains a recorded miss. This assertion
            // prevents confidently borrowing a neighbour's incompatible angle;
            // the existing frontal control separately requires real support.
            totalWrong+=wrong;
        }
        const auto output=qEnvironmentVariable("PF_BODY_REVIEW_REPORT");
        if(!output.isEmpty()) {QFile file(output);QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::NewOnly));
            QVERIFY(file.write(QJsonDocument(QJsonObject{{"schema",1},{"provider",qEnvironmentVariable("PF_BODY_REVIEW_PROVIDER")},{"cases",results}}).toJson())>0);}
        QCOMPARE(totalWrong,0);
    }
    void CancelledAndHeadOnlyWorkDoesNotOpenModelsOrCreateCache() {
        pfgpu::PoseEstimatorParams params;params.provider=pfgpu::Provider::Cpu;
        pfgpu::PoseEstimator pose("missing-pose.onnx",params);
        std::vector<pfcore::MotionWindow> windows(1);
        std::vector<pfcore::MotionMatch> matches(1);
        QTemporaryDir root;QVERIFY(root.isValid());
        const auto cache=pfcore::pathFromUtf8((root.path()+"/unused").toStdString());
        const auto run=[&](bool stopped) {return pfui::reviewMatchedBodyOrientations(windows,matches,pose,"missing-body.onnx",cache,{},pfgpu::Provider::Cpu,{},[=]{return stopped;});};
        const auto cancelled=run(true);QVERIFY(cancelled.error.empty());QCOMPARE(cancelled.requested,std::size_t(0));
        matches[0].headOnlyComparison=true;
        const auto head=run(false);QVERIFY(head.error.empty());QCOMPARE(head.bodyInferences,std::size_t(0));
        QVERIFY(!std::filesystem::exists(cache));
    }
    void realSourceCacheAndCancellation() {
        const auto video=qEnvironmentVariable("PF_BODY_REVIEW_VIDEO");
        const auto model=qEnvironmentVariable("PF_BODY_REVIEW_MODEL");
        const auto poseModel=qEnvironmentVariable("PF_BODY_REVIEW_POSE_MODEL");
        if(video.isEmpty()||model.isEmpty()||poseModel.isEmpty())QSKIP("Opt-in real-source body review");
        pfgpu::PoseEstimatorParams params;params.provider=qEnvironmentVariable("PF_BODY_REVIEW_PROVIDER")=="cuda"?pfgpu::Provider::Cuda:pfgpu::Provider::Cpu;
        params.intraOpThreads=4;
        pfgpu::PoseEstimator pose(poseModel.toStdString(),params);
        pfcore::VideoDecodeOptions options;options.threads=4;options.maxWidth=1280;options.maxHeight=720;
        pfcore::VideoDecoder decoder;decoder.open(video.toStdString(),options);decoder.setRgbaMaxDimensions(1280,720);
        pfcore::MotionWindow window;window.sourceId=video.toStdString();window.sceneStartSeconds=0;window.sceneEndSeconds=1.1;
        pfcore::DecodedFrame frame;
        for(double requested:{0.,.4,.8}) {
            decoder.seek(requested);
            do {QVERIFY(decoder.readNext(frame,false));}while(frame.timestampSeconds+1e-6<requested);
            QVERIFY(decoder.convertCurrentFrameToRgba(frame));
            const auto people=pose.infer({frame.width,frame.height,frame.rgba.data()});
            // Reviewed first Butcher shot: the target's torso is in the right
            // half. Select spatially, independently of body-angle output.
            const auto target=std::find_if(people.begin(),people.end(),[&](const auto& person) {
                const auto center=(person.left+person.right)*.5/frame.width;
                return center>.6&&center<.85&&(person.bottom-person.top)>.5*frame.height;
            });
            QVERIFY(target!=people.end());
            pfcore::PoseFrame observed;observed.timestampSeconds=frame.timestampSeconds;
            for(std::size_t p=0;p+2<target->keypoints.size();p+=3)observed.keypoints.push_back({target->keypoints[p],target->keypoints[p+1],target->keypoints[p+2]});
            window.frames.push_back(observed);
        }
        std::vector<pfcore::MotionWindow> windows{window};
        pfcore::MotionMatch match;match.leftStartSeconds=match.rightStartSeconds=0;match.leftEndSeconds=match.rightEndSeconds=.9;
        std::vector<pfcore::MotionMatch> matches{match};
        QTemporaryDir cache;QVERIFY(cache.isValid());
        const auto directory=pfcore::pathFromUtf8(cache.path().toStdString());
        const auto bodyModel=pfcore::pathFromUtf8(model.toStdString());
        std::map<std::string,std::string> generations{{window.sourceId,"reviewed-butcher-first-shot-v1"}};
        const auto run=[&](auto cancelled) {return pfui::reviewMatchedBodyOrientations(windows,matches,pose,bodyModel,directory,generations,params.provider,options,cancelled);};
        const auto cold=run([]{return false;});QVERIFY2(cold.error.empty(),cold.error.c_str());
        QCOMPARE(cold.requested,std::size_t(3));QCOMPARE(cold.cached,std::size_t(0));QCOMPARE(cold.poseInferences,std::size_t(3));
        const auto observations=windows[0].bodyOrientations;
        for(const auto& s:observations)qInfo("BODY_SAMPLE time=%.6f observed=%d yaw=%.3f reason=%s iou=%.3f margin=%.3f mirror=%.3f crop=%.3f",s.time,s.observed,s.yawDegrees,s.reason.c_str(),s.associationIou,s.associationMargin,s.mirrorDifference,s.cropDifference);
        QCOMPARE(cold.observed,std::size_t(3));
        for(const auto& s:observations)QVERIFY(s.yawDegrees>=-45&&s.yawDegrees<=45);
        const auto descriptor=pfcore::describeScene(windows[0],0,.9);
        QVERIFY(descriptor.body.yawDegrees);QVERIFY(!descriptor.body.pitchDegrees);
        const auto presented=pfui::sceneDescriptorMap(descriptor);QVERIFY(presented.contains("bodyYaw"));
        QCOMPARE(presented["bodyView"].toInt(),int(pfcore::viewDirection(*descriptor.body.yawDegrees)));
        QVERIFY(pfui::groupingEvidence(windows[0]).bodyOrientations==observations);
        const auto warm=run([]{return false;});QVERIFY2(warm.error.empty(),warm.error.c_str());
        QCOMPARE(warm.cached,std::size_t(3));QCOMPARE(warm.decoded,std::size_t(0));QCOMPARE(warm.poseInferences,std::size_t(0));QCOMPARE(warm.bodyInferences,std::size_t(0));
        QVERIFY(windows[0].bodyOrientations==observations);
        const auto cancelled=run([]{return true;});QCOMPARE(cancelled.requested,std::size_t(0));QCOMPARE(cancelled.bodyInferences,std::size_t(0));
        generations[window.sourceId]="reviewed-butcher-first-shot-v2";
        bool stop=false;
        const auto partial=pfui::reviewMatchedBodyOrientations(windows,matches,pose,bodyModel,directory,generations,params.provider,options,
            [&]{return stop;},[&](std::size_t done,std::size_t){if(done==1)stop=true;});
        QVERIFY2(partial.error.empty(),partial.error.c_str());QCOMPARE(partial.cached,std::size_t(0));QCOMPARE(partial.observed,std::size_t(1));
        const auto resumed=run([]{return false;});QVERIFY2(resumed.error.empty(),resumed.error.c_str());
        QCOMPARE(resumed.cached,std::size_t(1));QCOMPARE(resumed.poseInferences,std::size_t(2));
        QVERIFY(windows[0].bodyOrientations==observations);
        generations[window.sourceId]="";
        const auto uncached=run([]{return false;});QVERIFY2(uncached.error.empty(),uncached.error.c_str());QCOMPARE(uncached.cached,std::size_t(0));
        const auto repeated=run([]{return false;});QCOMPARE(repeated.cached,std::size_t(0));
    }
    void cacheRejectsIncompatibleAndUnownedMeasurements() {
        pfcore::BodyOrientationSample s;s.sourceId="film";s.time=.5;s.modelHash=pfcore::kBodyOrientationModelHash;
        s.observed=true;s.poseAssociated=true;s.yawDegrees=179;s.reason="observed";
        s.associationIou=.8;s.associationMargin=.3;s.mirrorDifference=3;s.cropDifference=4;
        const auto bytes=pfui::body_detail::serialize(s);
        const auto good=pfui::body_detail::read(bytes,"film",.5);QVERIFY(good);QCOMPARE(*good,s);
        QVERIFY(!pfui::body_detail::read(bytes,"other",.5));
        QVERIFY(!pfui::body_detail::read(bytes,"film",.6));
        for(const auto key:{"schema","model","yaw","crop","poseAssociated"}) {
            auto object=QJsonDocument::fromJson(QByteArray(reinterpret_cast<const char*>(bytes.data()),bytes.size())).object();
            object.remove(key);const auto broken=QJsonDocument(object).toJson();
            const std::vector<std::uint8_t> data(broken.begin(),broken.end());
            QVERIFY2(!pfui::body_detail::read(data,"film",.5),key);
        }
        s.poseAssociated=false;
        QVERIFY(!pfui::body_detail::read(pfui::body_detail::serialize(s),"film",.5));
        s.observed=false;s.reason="pose_unassociated";
        const auto unknown=pfui::body_detail::read(pfui::body_detail::serialize(s),"film",.5);
        QVERIFY(unknown);QVERIFY(!unknown->observed);
    }
};
QTEST_GUILESS_MAIN(BodyReviewTests)
#include "test_body_review.moc"
