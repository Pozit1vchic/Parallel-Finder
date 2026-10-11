#include <pfcore/SceneDetector.hpp>
#include <pfcore/VideoDecoder.hpp>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>

// Read source frames around a short review interval. Never touches user caches.
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
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
