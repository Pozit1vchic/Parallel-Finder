#include <pfcore/MotionMatcher.hpp>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <iostream>

// Replay diagnostic windows without decoding/inference, so a suspected miss
// can be inspected against the exact production matcher, not a second model.
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() != 4) { std::cerr << "usage: pf_matcher_probe windows.json left-index right-index\n"; return 2; }
    QFile file(args[1]);
    if (!file.open(QIODevice::ReadOnly)) return 2;
    QJsonParseError error;
    const auto json = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !json.isArray()) return 2;
    std::vector<pfcore::MotionWindow> windows;
    for (const auto& value : json.array()) {
        const auto o = value.toObject();
        pfcore::MotionWindow w;
        w.sourceId = o["source"].toString().toStdString();
        w.staticFrameSet = o["static"].toBool();
        w.hasSceneIndex = o.contains("scene");
        w.sceneIndex = static_cast<std::size_t>(o["scene"].toInteger());
        w.trackId = static_cast<std::size_t>(o["track"].toInteger());
        w.sceneStartSeconds = o["sceneStart"].toDouble(-1);
        w.sceneEndSeconds = o["sceneEnd"].toDouble(-1);
        w.faceConfidence = o["faceConfidence"].toDouble();
        w.appearanceConfidence = o["bodyConfidence"].toDouble();
        for (const auto& v : o["face"].toArray()) w.faceEmbedding.push_back(v.toDouble());
        for (const auto& v : o["body"].toArray()) w.appearanceEmbedding.push_back(v.toDouble());
        for (const auto& v : o["context"].toArray()) w.sceneContext.push_back(v.toDouble());
        for (const auto& v : o["frames"].toArray()) {
            const auto frame = v.toObject();
            pfcore::PoseFrame f; f.timestampSeconds = frame["time"].toDouble();
            for (const auto& p : frame["points"].toArray()) {
                const auto point = p.toArray();
                if (point.size() != 3) return 2;
                f.keypoints.push_back({point[0].toDouble(), point[1].toDouble(), point[2].toDouble()});
            }
            w.frames.push_back(std::move(f));
        }
        windows.push_back(std::move(w));
    }
    bool validA, validB;
    const auto a = args[2].toUInt(&validA), b = args[3].toUInt(&validB);
    if (!validA || !validB || a >= windows.size() || b >= windows.size()) return 2;
    pfcore::MotionMatcherParams params;
    params.allowStaticFrames = params.mirrorInvariant = params.requireAppearance = true;
    params.minTemporalFrames = 6;
    params.similarityThreshold = .70; // desktop fast-search acceptance profile
    params.timeWeight = .10;
    params.minAppearanceSimilarity = .76;
    params.staticArticulationSimilarityThreshold = .82;
    const auto match = pfcore::MotionMatcher(params).compare(windows[a], windows[b], a, b);
    std::cout << "similarity=" << match.similarity << " dtw=" << match.dtwDistance
              << " identity=" << match.appearanceVerified << " face=" << match.faceVerified
              << " headOnly=" << match.headOnlyComparison << "\n";
}
