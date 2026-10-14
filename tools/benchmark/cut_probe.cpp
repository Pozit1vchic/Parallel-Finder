#include <pfcore/SceneDetector.hpp>
#include <pfcore/VideoDecoder.hpp>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>
#include <algorithm>

// Read source frames around a short review interval. Never touches user caches.
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size()==6 && args[1]=="--confirm-samples") {
        QFile sampleFile(args[3]),windowFile(args[4]);
        if(!sampleFile.open(QIODevice::ReadOnly)||!windowFile.open(QIODevice::ReadOnly))return 2;
        const auto observed=QJsonDocument::fromJson(sampleFile.readAll()).object();
        if(observed["truncated"].toBool()||observed["width"].toInt()!=64||observed["height"].toInt()!=36)return 2;
        std::vector<std::vector<std::uint8_t>> buffers;
        std::vector<double> times,poses;
        const auto rows=observed["samples"].toArray();
        if(rows.size()>4096)return 2;
        for(const auto row:rows) {
            const auto item=row.toObject();
            const auto rgba=QByteArray::fromBase64(item["rgbaBase64"].toString().toLatin1());
            if(rgba.size()!=64*36*4)return 2;
            times.push_back(item["time"].toDouble(-1));
            buffers.emplace_back(rgba.begin(),rgba.end());
        }
        for(const auto window:QJsonDocument::fromJson(windowFile.readAll()).array())
            for(const auto frame:window.toObject()["frames"].toArray())
                poses.push_back(frame.toObject()["time"].toDouble(-1));
        std::sort(poses.begin(),poses.end());poses.erase(std::unique(poses.begin(),poses.end()),poses.end());
        std::vector<pfcore::SceneSample> samples;
        for(std::size_t i=0;i<times.size();++i)samples.push_back({times[i],64,36,buffers[i]});
        const pfcore::SceneDetector detector;
        std::vector<pfcore::SceneBoundary> proposals;
        QJsonArray proposalJson;
        for(const auto candidate:detector.highContrastCutCandidates(samples)) {
            proposals.push_back({candidate.afterSeconds,candidate.score});
            proposalJson.append(QJsonObject{{"before",candidate.beforeSeconds},{"after",candidate.afterSeconds}});
        }
        pfcore::VideoDecoder decoder;decoder.open(args[2].toStdString());
        QJsonArray confirmed;
        for(const auto cut:detector.refineSupportedBoundaries(decoder,proposals,times,poses,{}, {},true))
            confirmed.append(QJsonObject{{"time",cut.timestampSeconds},{"score",cut.score}});
        QFile output(args[5]);if(!output.open(QIODevice::WriteOnly))return 3;
        output.write(QJsonDocument(QJsonObject{{"source",args[2]},
            {"scope","native detector evidence; not manual ground truth"},
            {"proposals",proposalJson},{"boundaries",confirmed}}).toJson());
        std::cout<<proposals.size()<<" proposals, "<<confirmed.size()<<" native-supported cuts\n";
        return 0;
    }
    if (args.size() != 5 && args.size() != 7) {
        std::cerr << "Usage: pf_cut_probe VIDEO START END OUTPUT.json [SAMPLED_BOUNDARY POSE_TIME]\n"; return 2;
    }
    bool startValid = false, endValid = false;
    const double start = args[2].toDouble(&startValid), end = args[3].toDouble(&endValid);
    if (!startValid || !endValid || start < 0 || end <= start || end - start > 30) return 2;
    try {
        pfcore::VideoDecoder decoder; pfcore::VideoDecodeOptions options; options.threads = 2;
        decoder.open(args[1].toStdString(), options);
        const auto cuts = pfcore::SceneDetector().detectInterval(decoder, start, end);
        QJsonArray boundaries;
        for (const auto& cut : cuts)
            boundaries.append(QJsonObject{{"time", cut.timestampSeconds}, {"score", cut.score}});
        QJsonArray refined;
        if(args.size()==7) {
            bool boundaryValid=false,poseValid=false;
            const double boundary=args[5].toDouble(&boundaryValid),pose=args[6].toDouble(&poseValid);
            if(!boundaryValid || !poseValid || boundary<=start || boundary>end || pose<start || pose>=boundary)return 2;
            const pfcore::SceneBoundary sampled{boundary,1};
            const double times[]{start,boundary},poses[]{pose};
            for(const auto& result:pfcore::SceneDetector().refineSupportedBoundaries(decoder,
                std::span(&sampled,1),times,poses))
                refined.append(QJsonObject{{"time",result.timestampSeconds},{"score",result.score}});
        }
        QFile output(args[4]); if (!output.open(QIODevice::WriteOnly)) return 3;
        output.write(QJsonDocument(QJsonObject{{"source", args[1]}, {"start", start}, {"end", end},
            {"fps", decoder.info().frameRate}, {"boundaries", boundaries},{"refinedSampledBoundaries",refined}}).toJson());
        std::cout << "Verified " << cuts.size() << " internal boundaries\n";
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 4; }
}
