#include <pfcore/MotionMatcher.hpp>
#include <pfcore/MotionRanker.hpp>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <iostream>
#include <chrono>
#include <set>

// Replay diagnostic windows without decoding/inference, so a suspected miss
// can be inspected against the exact production matcher, not a second model.
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() < 3) { std::cerr << "usage: pf_matcher_probe windows.json left-index right-index | --all [params.json] | --batch pairs.json [params.json]\n"; return 2; }
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
    pfcore::MotionMatcherParams params;
    params.allowStaticFrames = params.mirrorInvariant = params.requireAppearance = true;
    params.minTemporalFrames = 6;
    params.similarityThreshold = .70; // desktop fast-search acceptance profile
    params.timeWeight = .10;
    params.minAppearanceSimilarity = .76;
    params.staticArticulationSimilarityThreshold = .82;
    params.candidateThreshold = .48;
    params.minRepeatGapSec = 8;
    params.sameFileGapSec = 3;
    params.sameSourceGapFloorSec = 12;
    params.duplicateWindowSec = 2;
    params.noiseFactor = 1.25;
    params.maxUniqueResults = 50;
    const bool all = args[2] == "--all", batch = args[2] == "--batch";
    const int configIndex = all ? 3 : batch ? 4 : 4;
    if (args.size() > configIndex) {
        QFile config(args[configIndex]);
        if (!config.open(QIODevice::ReadOnly)) return 2;
        const auto doc = QJsonDocument::fromJson(config.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) return 2;
        const auto c = doc.object();
#define PF_NUMBER(name) if (c.contains(#name)) params.name = c[#name].toDouble()
#define PF_BOOL(name) if (c.contains(#name)) params.name = c[#name].toBool()
        PF_NUMBER(similarityThreshold); PF_NUMBER(candidateThreshold); PF_NUMBER(minRepeatGapSec);
        PF_NUMBER(sameFileGapSec); PF_NUMBER(crossFileGapSec); PF_NUMBER(duplicateWindowSec);
        PF_NUMBER(noiseFactor); PF_NUMBER(timeWeight); PF_NUMBER(maxUniqueResults);
        PF_NUMBER(maxResultsPerShot); PF_NUMBER(minTemporalFrames); PF_NUMBER(minMotionSpanSec);
        PF_NUMBER(maxComparisonThreads);
        PF_NUMBER(staticPoseSimilarityThreshold); PF_NUMBER(staticArticulationSimilarityThreshold);
        PF_NUMBER(minAppearanceSimilarity); PF_NUMBER(minFaceSimilarity); PF_NUMBER(sameSceneContextThreshold);
        PF_NUMBER(minHeadFaceSimilarity);
        PF_NUMBER(sameSourceGapFloorSec); PF_NUMBER(minMotionRange); PF_NUMBER(motionDeltaThreshold);
        PF_BOOL(requireAppearance); PF_BOOL(requireSameTrackWithinSource); PF_BOOL(mirrorInvariant);
        PF_BOOL(normalizeSize); PF_BOOL(expandedSearch); PF_BOOL(allowStaticFrames);
#undef PF_NUMBER
#undef PF_BOOL
    }
    const auto resultJson = [](const pfcore::MotionMatch& m) {
        return QJsonObject{{"leftIndex", static_cast<qint64>(m.leftIndex)},
            {"rightIndex", static_cast<qint64>(m.rightIndex)},
            {"leftSource",QString::fromStdString(m.leftSourceId)},
            {"rightSource",QString::fromStdString(m.rightSourceId)},
            {"leftStart",m.leftStartSeconds},{"leftEnd",m.leftEndSeconds},
            {"rightStart",m.rightStartSeconds},{"rightEnd",m.rightEndSeconds},
            {"similarity",m.similarity},{"dtwDistance",m.dtwDistance},
            {"identityVerified",m.appearanceVerified},{"faceVerified",m.faceVerified},
            {"appearanceSimilarity",m.appearanceSimilarity},{"sceneSimilarity",m.sceneSimilarity},
            {"headOnlyComparison",m.headOnlyComparison},{"rankScore",m.rankScore}};
    };
    if (all) {
        const auto start = std::chrono::steady_clock::now();
        auto matches = pfcore::MotionMatcher(params).findAllPairs(windows);
        const auto matcherMs = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        pfcore::MotionRanker::rank(matches,windows);
        QJsonArray results, inputs;
        std::set<std::string> sources;
        for (const auto& w : windows) sources.insert(w.sourceId);
        for (const auto& s : sources) inputs.append(QString::fromStdString(s));
        for (const auto& match : matches) results.append(resultJson(match));
        const QJsonObject report{{"inputVideos",inputs},{"results",results},{"elapsedMs",matcherMs},
            {"matcherOnly",true},{"windowCount",static_cast<qint64>(windows.size())}};
        std::cout << QJsonDocument(report).toJson(QJsonDocument::Compact).constData() << "\n";
        return 0;
    }
    if (batch) {
        if (args.size() < 4) return 2;
        QFile pairsFile(args[3]);
        if (!pairsFile.open(QIODevice::ReadOnly)) return 2;
        const auto pairs = QJsonDocument::fromJson(pairsFile.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !pairs.isArray()) return 2;
        QJsonArray results;
        const pfcore::MotionMatcher matcher(params);
        for (const auto& v : pairs.array()) {
            const auto p = v.toObject();
            const auto a = p["a"].toInteger(-1), b = p["b"].toInteger(-1);
            if (a < 0 || b < 0 || static_cast<std::size_t>(a) >= windows.size()
                || static_cast<std::size_t>(b) >= windows.size()) return 2;
            auto result = resultJson(matcher.compare(windows[a],windows[b],a,b));
            result["caseId"] = p["id"];
            results.append(result);
        }
        std::cout << QJsonDocument(results).toJson(QJsonDocument::Compact).constData() << "\n";
        return 0;
    }
    if (args.size() < 4) return 2;
    bool validA, validB;
    const auto a = args[2].toUInt(&validA), b = args[3].toUInt(&validB);
    if (!validA || !validB || a >= windows.size() || b >= windows.size()) return 2;
    const auto match = pfcore::MotionMatcher(params).compare(windows[a], windows[b], a, b);
    std::cout << "similarity=" << match.similarity << " dtw=" << match.dtwDistance
              << " identity=" << match.appearanceVerified << " face=" << match.faceVerified
              << " headOnly=" << match.headOnlyComparison << "\n";
}
