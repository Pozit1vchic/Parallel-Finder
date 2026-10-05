#include <pfgpu/FaceEstimator.hpp>
#include <QCoreApplication>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
#include <cmath>

// Audit the exact application's face crop/landmarks/features on fixed frames.
// CPU only; no model download and no mutation of application preferences.
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() != 4) {
        std::cerr << "usage: pf_face_probe samples.json yunet.onnx sface.onnx\n";
        return 2;
    }
    QFile samples(args[1]);
    if (!samples.open(QIODevice::ReadOnly)) return 2;
    QJsonParseError error;
    const auto input = QJsonDocument::fromJson(samples.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !input.isArray()) return 2;
    try {
        pfgpu::FaceEstimator estimator(args[2].toStdString(), args[3].toStdString(), pfgpu::Provider::Cpu);
        QJsonArray output;
        for (const auto& entry : input.array()) {
            auto item = entry.toObject();
            auto image = QImage(item["image"].toString()).convertToFormat(QImage::Format_RGBA8888);
            const auto box = item["box"].toArray();
            if (image.isNull() || box.size() != 4 || image.bytesPerLine() != image.width()*4) return 2;
            if(item["exposureRetry"].toBool())for(int y=std::max(0,static_cast<int>(box[1].toDouble()));y<std::min(image.height(),static_cast<int>(box[3].toDouble()));++y)
                for(int x=std::max(0,static_cast<int>(box[0].toDouble()));x<std::min(image.width(),static_cast<int>(box[2].toDouble()));++x)
                    for(int c=0;c<3;++c) {
                        auto& v=image.bits()[(static_cast<std::size_t>(y)*image.width()+x)*4+c];
                        v=static_cast<unsigned char>(std::sqrt(v/255.0)*255);
                    }
            pfgpu::FaceRecognitionDiagnostics diagnostic;
            const auto nose=item["nose"].toArray();std::array<double,2> expected{};const std::array<double,2>* hint=nullptr;
            if(nose.size()==2) {expected={nose[0].toDouble(),nose[1].toDouble()};hint=&expected;}
            const pfgpu::ReIdImage inputImage{image.width(), image.height(), image.constBits(),
                static_cast<float>(box[0].toDouble()), static_cast<float>(box[1].toDouble()),
                static_cast<float>(box[2].toDouble()), static_cast<float>(box[3].toDouble())};
            const auto anchors=item["eyesAndNose"].toArray();
            std::array<double,6> poseAnchors{};
            for(int i=0;i<anchors.size() && i<6;++i)poseAnchors[i]=anchors[i].toDouble();
            const auto features = anchors.size()==6 ? estimator.inferFromPose(inputImage,poseAnchors,&diagnostic)
                : estimator.infer(inputImage, &diagnostic,item["minimumDetectionConfidence"].toDouble(.85),hint);
            QJsonArray face, landmarks, transform;
            for (const auto value : features) face.append(value);
            for (const auto value : diagnostic.landmarks) landmarks.append(value);
            for (const auto value : diagnostic.canonicalToSource) transform.append(value);
            item["face"] = face;
            item["accepted"] = diagnostic.accepted;
            item["detectorScore"] = diagnostic.detectorScore;
            item["landmarks"] = landmarks;
            item["canonicalToSource"] = transform;
            output.append(item);
        }
        std::cout << QJsonDocument(output).toJson(QJsonDocument::Compact).constData() << '\n';
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
