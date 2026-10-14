#include <pfcore/VideoDecoder.hpp>
#include <pfcore/PoseAssociation.hpp>
#include <pfgpu/PoseEstimator.hpp>
#include <pfgpu/ReIdEstimator.hpp>
#include <pfgpu/FaceEstimator.hpp>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFileInfo>
#include <QDateTime>
#include <QCryptographicHash>
#include <iostream>
#include <cmath>

// Re-detect ALL people at explicitly requested source timestamps. This is
// evidence for cache repair, never an automatic source-lead admission rule.
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);const auto args=app.arguments();
    if(args.size()!=8) {
        std::cerr<<"usage: pf_identity_refresh_probe SOURCE REQUESTS.json POSE.onnx REID.onnx YUNET.onnx SFACE.onnx OUTPUT.json\n";
        return 2;
    }
    QFile requests(args[2]);if(!requests.open(QIODevice::ReadOnly))return 2;
    QJsonParseError error;const auto input=QJsonDocument::fromJson(requests.readAll(),&error);
    if(error.error!=QJsonParseError::NoError||!input.isArray()||input.array().isEmpty()||input.array().size()>256)return 2;
    QFile output(args[7]);if(output.exists())return 2;
    try {
        pfcore::VideoDecoder decoder;pfcore::VideoDecodeOptions options;options.threads=2;
        decoder.open(args[1].toStdString(),options);decoder.setRgbaMaxDimensions(1280,720);
        pfgpu::PoseEstimatorParams poseParams;poseParams.provider=pfgpu::Provider::Cpu;poseParams.intraOpThreads=8;
        pfgpu::PoseEstimator pose(args[3].toStdString(),poseParams);
        pfgpu::ReIdEstimatorParams bodyParams;bodyParams.provider=pfgpu::Provider::Cpu;bodyParams.intraOpThreads=8;
        pfgpu::ReIdEstimator reid(args[4].toStdString(),bodyParams);
        pfgpu::FaceEstimator face(args[5].toStdString(),args[6].toStdString(),pfgpu::Provider::Cpu);
        const auto floats=[](const auto& values) {QJsonArray result;for(const auto value:values)result.append(double(value));return result;};
        QJsonArray models;
        for(int index=3;index<=6;++index) {
            QFile model(args[index]);QCryptographicHash hash(QCryptographicHash::Sha256);
            if(!model.open(QIODevice::ReadOnly)||!hash.addData(&model))
                throw std::runtime_error("Cannot fingerprint inference model");
            models.append(QJsonObject{{"path",args[index]},{"sha256",QString::fromLatin1(hash.result().toHex())}});
        }
        const QFileInfo sourceBefore(args[1]);
        const auto sourceSize=sourceBefore.size();
        const auto sourceModified=sourceBefore.lastModified();
        QJsonArray observations;double previous=-1;
        for(const auto value:input.array()) {
            if(!value.isDouble()&&!value.isObject())return 2;
            const auto request=value.toObject();
            const double time=value.isDouble() ? value.toDouble():request["time"].toDouble(-1);
            if(!std::isfinite(time)||time<0||time<=previous||time>=decoder.info().durationSeconds)return 2;
            previous=time;decoder.seek(time);pfcore::DecodedFrame image;
            bool decoded=false;
            while(decoder.readNext(image,false))if(image.timestampSeconds>=time-1e-6) {decoded=true;break;}
            if(!decoded||!decoder.convertCurrentFrameToRgba(image))
                throw std::runtime_error("Cannot decode requested observation");
            const double frameDuration=1./std::max(1.,decoder.info().frameRate);
            if(image.timestampSeconds>time+frameDuration*1.1)
                throw std::runtime_error("Source timestamp too far from requested observation");
            QJsonArray people;
            std::vector<pfcore::PoseFrame> detectedPoses;
            for(const auto& person:pose.infer({image.width,image.height,image.rgba.data()})) {
                QJsonArray points;
                pfcore::PoseFrame detected;detected.timestampSeconds=image.timestampSeconds;
                for(std::size_t i=0;i+2<person.keypoints.size();i+=3)
                {
                    points.append(QJsonArray{person.keypoints[i],person.keypoints[i+1],person.keypoints[i+2]});
                    detected.keypoints.push_back({person.keypoints[i],person.keypoints[i+1],person.keypoints[i+2]});
                }
                detectedPoses.push_back(std::move(detected));
                const pfgpu::ReIdImage crop{image.width,image.height,image.rgba.data(),person.left,person.top,person.right,person.bottom};
                std::vector<float> body,identity;
                pfgpu::FaceRecognitionDiagnostics diagnostic;
                if(person.confidence>=.4) {
                    if(person.right-person.left>=16&&person.bottom-person.top>=32)body=reid.infer(crop);
                    std::array<double,2> nose{};const std::array<double,2>* hint=nullptr;
                    if(person.keypoints.size()>=3&&person.keypoints[2]>=.75) {
                        nose={person.keypoints[0],person.keypoints[1]};hint=&nose;
                    }
                    identity=face.infer(crop,&diagnostic,.85,hint);
                }
                people.append(QJsonObject{{"box",QJsonArray{person.left,person.top,person.right,person.bottom}},
                    {"confidence",person.confidence},{"points",points},{"body",floats(body)},
                    {"face",floats(identity)},{"faceAccepted",diagnostic.accepted}});
            }
            QJsonValue association(QJsonValue::Null);
            if(value.isObject()) {
                pfcore::PoseFrame cached;cached.timestampSeconds=time;
                for(const auto p:request["points"].toArray()) {
                    const auto point=p.toArray();if(point.size()!=3)return 2;
                    cached.keypoints.push_back({point[0].toDouble(),point[1].toDouble(),point[2].toDouble()});
                }
                if(const auto matched=pfcore::associateObservedPose(cached,detectedPoses,image.width,image.height))
                    association=QJsonObject{{"candidate",static_cast<qint64>(matched->candidate)},
                        {"commonJoints",static_cast<qint64>(matched->commonJoints)},
                        {"normalizedMedianError",matched->normalizedMedianError}};
            }
            observations.append(QJsonObject{{"requested",time},{"time",image.timestampSeconds},
                {"width",image.width},{"height",image.height},{"people",people},{"association",association}});
            std::cerr<<"Refreshed "<<observations.size()<<"/"<<input.array().size()<<" observations\n";
        }
        const QFileInfo sourceAfter(args[1]);
        if(sourceAfter.size()!=sourceSize||sourceAfter.lastModified()!=sourceModified)
            throw std::runtime_error("Source changed during local refresh");
        if(!output.open(QIODevice::WriteOnly|QIODevice::NewOnly))return 3;
        const auto bytes=QJsonDocument(QJsonObject{{"source",args[1]},
            {"scope","local re-detection and appearance observations; no source-lead or pair admission"},
            {"provider","cpu"},{"poseModel",args[3]},{"reidModel",args[4]},
            {"faceDetector",args[5]},{"faceRecognizer",args[6]},{"models",models},
            {"sourceSize",sourceSize},{"sourceModifiedMs",sourceModified.toMSecsSinceEpoch()},
            {"rgbaMaxWidth",1280},{"rgbaMaxHeight",720},{"observations",observations}}).toJson();
        if(output.write(bytes)!=bytes.size())return 3;
    }catch(const std::exception& exception){std::cerr<<exception.what()<<'\n';return 4;}
    return 0;
}
