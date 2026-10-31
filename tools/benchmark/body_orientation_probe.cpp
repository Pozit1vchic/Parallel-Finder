#include <pfgpu/BodyOrientationEstimator.hpp>
#include <QCoreApplication>
#include <QImage>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <cstdio>
#include <cmath>
#include <stdexcept>

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    const auto args=app.arguments();
    if(args.size()!=6) {std::fprintf(stderr,"Usage: pf_body_orientation_probe MODEL LABELS IMAGE_DIRECTORY OUTPUT cpu|cuda\n");return 2;}
    if(QFileInfo::exists(args[4]))return 2;
    QFile model(args[1]),labelsFile(args[2]);if(!model.open(QIODevice::ReadOnly)||!labelsFile.open(QIODevice::ReadOnly))return 2;
    QCryptographicHash hash(QCryptographicHash::Sha256);if(!hash.addData(&model))return 2;
    if(hash.result().toHex()!="3bdfefba3f0bbcd23454ceefaa3088c2e95a7b9d120502899f65249ae98e1c93")return 2;
    const auto provider=pfgpu::parseProvider(args[5].toStdString());if(!provider||(*provider!=pfgpu::Provider::Cpu&&*provider!=pfgpu::Provider::Cuda))return 2;
    const auto labels=QJsonDocument::fromJson(labelsFile.readAll()).object();
    if(labels.value("controls").toArray().isEmpty())return 2;
    pfgpu::BodyOrientationEstimator estimator(args[1].toStdString(),*provider,4);
    QJsonArray results;int supported=0,correct=0,wrong=0;
    try {
        for(const auto entry:labels.value("controls").toArray()) {
            auto row=entry.toObject();const auto filename=QDir(args[3]).filePath(row.value("image").toString());
            auto image=QImage(filename).convertToFormat(QImage::Format_RGBA8888);
            if(image.isNull()||image.bytesPerLine()!=image.width()*4)throw std::runtime_error("Invalid control image");
            if(row.contains("rgbaSha256")&&QString::fromLatin1(QCryptographicHash::hash(
                QByteArrayView(reinterpret_cast<const char*>(image.constBits()),image.sizeInBytes()),QCryptographicHash::Sha256).toHex())!=row.value("rgbaSha256").toString())
                throw std::runtime_error("Control frame pixels differ from the reviewed fixture");
            const auto box=row.value("expectedBodyBox").toArray();if(box.size()!=4)throw std::runtime_error("Invalid control rectangle");
            const pfgpu::BodyOrientationRegion subject{box[0].toDouble()*image.width(),box[1].toDouble()*image.height(),
                box[2].toDouble()*image.width(),box[3].toDouble()*image.height()};
            const pfgpu::PoseImage view{image.width(),image.height(),image.constBits()};
            QElapsedTimer timer;timer.start();
            const auto original=estimator.infer(view),mirrored=estimator.infer(view,true);
            const auto observed=pfgpu::associateBodyOrientations({subject},original,mirrored).front();
            row.insert("elapsedMs",timer.nsecsElapsed()/1e6);row.insert("associationIou",observed.associationIou);
            row.insert("associationMargin",observed.associationMargin);row.insert("mirrorDifferenceDegrees",observed.mirrorDifferenceDegrees);
            row.insert("status",QString::fromLatin1(pfgpu::bodyYawStatusCode(observed.status)));
            row.insert("yawDegrees",observed.yawDegrees?QJsonValue(*observed.yawDegrees):QJsonValue::Null);
            QJsonArray views;
            for(const auto& detections:{original,mirrored}) {
                QJsonArray decoded;
                for(const auto& d:detections)decoded.push_back(QJsonObject{{"box",QJsonArray{d.left/image.width(),d.top/image.height(),d.right/image.width(),d.bottom/image.height()}},
                    {"detectionScore",d.detectionScore},{"yawDegrees",d.yawDegrees}});
                views.push_back(decoded);
            }
            row.insert("detections",views);
            if(observed.yawDegrees) {
                ++supported;const auto range=row.value("acceptableYawDegrees").toArray();
                double yaw=*observed.yawDegrees;if(yaw<range[0].toDouble())yaw+=360;
                const bool accepted=yaw>=range[0].toDouble()&&yaw<=range[1].toDouble();
                if(accepted)++correct;else ++wrong;
                row.insert("withinReviewedInterval",accepted);
            }
            results.push_back(row);
        }
    } catch(const std::exception& e) {std::fprintf(stderr,"%s\n",e.what());return 1;}
    QJsonObject output{{"schema",1},{"modelSha256",QString::fromLatin1(hash.result().toHex())},
        {"provider",args[5]},{"annotationMethod",labels.value("annotationMethod")},
        {"supported",supported},{"correctCoarseView",correct},{"wrongCoarseView",wrong},{"controls",results}};
    QSaveFile file(args[4]);if(!file.open(QIODevice::WriteOnly))return 1;
    const auto bytes=QJsonDocument(output).toJson();if(file.write(bytes)!=bytes.size()||!file.commit())return 1;
    std::printf("controls=%lld supported=%d coarse_correct=%d coarse_wrong=%d\n",static_cast<long long>(results.size()),supported,correct,wrong);
    return 0;
}
