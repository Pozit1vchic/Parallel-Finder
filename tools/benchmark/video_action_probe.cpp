#include <pfcore/VideoDecoder.hpp>
#include <pfgpu/VideoActionEstimator.hpp>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QElapsedTimer>
#include <algorithm>
#include <cmath>
#include <iostream>

// Native decoder + estimator audit; never changes app results or source caches.
int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);
    if(argc!=5) {std::cerr<<"usage: pf_video_action_probe CASES.json MODEL.onnx cpu|cuda OUTPUT.json\n";return 2;}
    const QString provider=QString::fromUtf8(argv[3]);
    if(provider!="cpu" && provider!="cuda")return 2;
    QFile input(QString::fromUtf8(argv[1]));
    if(!input.open(QIODevice::ReadOnly))return 2;
    const auto cases=QJsonDocument::fromJson(input.readAll()).array();
    if(cases.isEmpty() || cases.size()>16 || QFileInfo::exists(QString::fromUtf8(argv[4])))return 2;
    try {
        pfgpu::VideoActionEstimator estimator(argv[2],provider=="cuda"?pfgpu::Provider::Cuda:pfgpu::Provider::Cpu,8);
        QJsonArray results;
        for(const auto entry:cases) {
            const auto row=entry.toObject();const auto source=row["source"].toString();
            const double start=row["start"].toDouble(-1),end=row["end"].toDouble(-1);
            if(!std::isfinite(start)||!std::isfinite(end)||start<0||end-start<.5||end-start>3)return 2;
            QFile roiFile(row["roi"].toString());if(!roiFile.open(QIODevice::ReadOnly))return 2;
            const auto roi=QJsonDocument::fromJson(roiFile.readAll()).object();
            if(QFileInfo(roi["source"].toString()).absoluteFilePath()!=QFileInfo(source).absoluteFilePath())return 2;
            const auto observation=roi["observations"].toArray().at(0).toObject();
            if(observation["association"].isNull() || observation["time"].toDouble()<start || observation["time"].toDouble()>=end)return 2;
            const int candidate=observation["association"].toObject()["candidate"].toInt(-1);
            const auto people=observation["people"].toArray();if(candidate<0 || candidate>=people.size())return 2;
            const auto box=people[candidate].toObject()["box"].toArray();if(box.size()!=4)return 2;
            const std::array<pfgpu::VideoActionSubject,1> subjects{{{float(box[0].toDouble()),float(box[1].toDouble()),
                float(box[2].toDouble()),float(box[3].toDouble())}}};
            const QFileInfo before(source);const auto sourceSize=before.size();const auto sourceModified=before.lastModified();
            QElapsedTimer clock;clock.start();
            pfcore::VideoDecoder decoder;pfcore::VideoDecodeOptions options;options.threads=2;
            decoder.open(source.toStdString(),options);decoder.setRgbaMaxDimensions(1280,720);decoder.seek(start);
            std::vector<pfcore::DecodedFrame> decoded;
            pfcore::DecodedFrame frame;
            while(decoder.readNext(frame,false)) {
                if(frame.timestampSeconds<start-1e-6)continue;
                if(frame.timestampSeconds>=end-1e-6)break;
                if(decoded.size()>=192 || !decoder.convertCurrentFrameToRgba(frame))return 3;
                decoded.push_back(std::move(frame));
            }
            if(decoded.size()<5)return 3;
            const QFileInfo after(source);
            if(after.size()!=sourceSize || after.lastModified()!=sourceModified)return 3;
            QJsonArray timestamps;
            std::vector<pfgpu::VideoActionFrame> frames;
            for(const auto& decodedFrame:decoded) {
                if(decodedFrame.width!=observation["width"].toInt() || decodedFrame.height!=observation["height"].toInt())return 3;
                frames.push_back({decodedFrame.width,decodedFrame.height,decodedFrame.rgba.data()});
                timestamps.append(decodedFrame.timestampSeconds);
            }
            QJsonObject variants;
            for(int variant=0;variant<3;++variant) {
                auto ordered=frames;QString name="forward";
                if(variant==1) {std::reverse(ordered.begin(),ordered.end());name="reverse";}
                if(variant==2) {std::fill(ordered.begin(),ordered.end(),frames[16*(frames.size()-1)/31]);name="repeat_midpoint";}
                const auto scores=estimator.infer(ordered,subjects).at(0);
                QJsonArray values;for(const auto score:scores)values.append(score);
                variants[name]=QJsonObject{{"walk",scores[13]},{"scores",values}};
            }
            results.append(QJsonObject{{"id",row["id"]},{"source",source},{"start",start},{"end",end},
                {"sourceBytes",sourceSize},{"sourceModifiedMs",sourceModified.toMSecsSinceEpoch()},
                {"framePts",timestamps},{"variants",variants},{"elapsedMs",clock.elapsed()}});
            std::cerr<<"Reviewed "<<results.size()<<"/"<<cases.size()<<" native clips\n";
        }
        QFile output(QString::fromUtf8(argv[4]));if(!output.open(QIODevice::WriteOnly|QIODevice::NewOnly))return 3;
        const auto bytes=QJsonDocument(QJsonObject{{"scope","native person-action model audit; not identity, continuity or app admission"},
            {"providerRequested",provider},{"model",QString::fromUtf8(argv[2])},{"results",results}}).toJson();
        if(output.write(bytes)!=bytes.size())return 3;
    }catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 4;}
    return 0;
}
