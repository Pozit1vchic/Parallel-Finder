#include <pfgpu/FaceEstimator.hpp>
#include <QCoreApplication>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>

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
            const auto image = QImage(item["image"].toString()).convertToFormat(QImage::Format_RGBA8888);
            const auto box = item["box"].toArray();
            if (image.isNull() || box.size() != 4 || image.bytesPerLine() != image.width()*4) return 2;
            pfgpu::FaceRecognitionDiagnostics diagnostic;
            const auto features = estimator.infer({image.width(), image.height(), image.constBits(),
                static_cast<float>(box[0].toDouble()), static_cast<float>(box[1].toDouble()),
                static_cast<float>(box[2].toDouble()), static_cast<float>(box[3].toDouble())}, &diagnostic);
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
