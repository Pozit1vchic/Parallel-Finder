#include <pfgpu/HeadPoseEstimator.hpp>
#include <pfcore/HeadOrientation.hpp>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>

// Read-only model/crop audit. Outputs belong to the explicit experiment dir.
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    QFile input(QString::fromUtf8(argv[1]));if(!input.open(QIODevice::ReadOnly))return 3;
    const auto root=QJsonDocument::fromJson(input.readAll()).object();QJsonArray results;
    try {
        for(const auto value:root["images"].toArray()) {
            const auto row=value.toObject();QImage image(row["path"].toString());
            if(image.isNull())return 4;
            image=image.convertToFormat(QImage::Format_RGBA8888);
            const auto crop=row["crop"].toArray();if(crop.size()!=4)return 5;
            const pfgpu::ReIdImage view{image.width(),image.height(),image.constBits(),
                static_cast<float>(crop[0].toDouble()),static_cast<float>(crop[1].toDouble()),
                static_cast<float>(crop[2].toDouble()),static_cast<float>(crop[3].toDouble())};
            const auto prepared=pfgpu::prepareHeadPoseInput(view);
            if(row.contains("tensorPath")) {
                QFile tensor(row["tensorPath"].toString());if(!tensor.open(QIODevice::WriteOnly|QIODevice::NewOnly))return 6;
                const auto size=static_cast<qint64>(prepared.values.size()*sizeof(float));
                if(tensor.write(reinterpret_cast<const char*>(prepared.values.data()),size)!=size)return 6;
            }
            QJsonObject result{{"id",row["id"]}};
            if(root.contains("modelPath")) {
                pfgpu::HeadPoseEstimator estimator(root["modelPath"].toString().toStdString(),
                    root["provider"].toString()=="cuda" ? pfgpu::Provider::Cuda:pfgpu::Provider::Cpu);
                QJsonArray rotation;
                if(const auto r=estimator.infer(view))for(const auto v:*r)rotation.append(v);
                result["rotation"]=rotation;
            }
            results.append(result);
        }
        QJsonArray actions;
        for(const auto value:root["results"].toArray()) {
            const auto row=value.toObject();std::vector<pfcore::HeadOrientationSample> samples;
            for(const auto sampleValue:row["samples"].toArray()) {
                const auto sample=sampleValue.toObject();pfcore::HeadOrientationSample s;
                s.time=sample["time"].toDouble();s.observed=sample["accepted"].toBool();
                const auto matrix=sample["rotation"].toArray();
                if(s.observed && matrix.size()!=3)return 7;
                if(s.observed)for(int i=0;i<3;++i)for(int j=0;j<3;++j)s.rotation[i*3+j]=matrix[i].toArray()[j].toDouble();
                samples.push_back(s);
            }
            if(samples.empty())continue;
            const auto classified=pfcore::classifyHeadOrientation(samples,samples.front().time,samples.back().time);
            actions.append(QJsonObject{{"caseId",row["caseId"]},
                {"gesture",pfcore::MovementClassifier::gestureName(classified.gesture)},
                {"evidenceStrength",classified.gestureConfidence}});
        }
        QFile output(QString::fromUtf8(argv[2]));if(!output.open(QIODevice::WriteOnly|QIODevice::NewOnly))return 8;
        output.write(QJsonDocument(QJsonObject{{"images",results},{"actions",actions}}).toJson());
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 9;}
    return 0;
}
