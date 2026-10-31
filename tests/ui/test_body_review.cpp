#include <QtTest/QTest>
#include <BodyOrientationCache.h>
#include <BodyOrientationReview.h>
#include <pfcore/PathEncoding.hpp>
#include <pfcore/SceneDescriptor.hpp>
#include <ScenePresentation.h>
#include <ResultClassification.h>
#include <QTemporaryDir>
#include <algorithm>

class BodyReviewTests:public QObject {
    Q_OBJECT
private slots:
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
