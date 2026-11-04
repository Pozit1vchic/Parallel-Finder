#include <pfcore/MotionMatcher.hpp>
#include <algorithm>
#include <pfcore/MotionRanker.hpp>
#include <pfcore/DominantPerson.hpp>
#include <pfcore/MovementClassifier.hpp>
#if __has_include(<pfcore/SceneFamily.hpp>)
#include <pfcore/SceneFamily.hpp>
#define PF_ORGANIZATION_PROBE 1
#endif
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <iostream>
#include <chrono>
#include <cmath>
#include <set>
#include <array>

const char* rejectionName(pfcore::MotionRejection reason)
{
    constexpr std::array names = {"none", "shot_boundary", "source_overlap", "same_shot",
        "nearby_context", "identity", "head_identity", "temporal_support", "motion_evidence",
        "body_visibility", "limb_support", "active_body", "alignment", "source_gap", "direction",
        "scene_context", "head_geometry", "static_geometry", "measured_identity", "copied_footage",
        "body_only", "same_camera", "score"};
    static_assert(names.size() == static_cast<std::size_t>(pfcore::MotionRejection::Score) + 1);
    const auto index = static_cast<std::size_t>(reason);
    return index < names.size() ? names[index] : "unknown";
}

// Replay diagnostic windows without decoding/inference, so a suspected miss
// can be inspected against the exact production matcher, not a second model.
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() < 3) { std::cerr << "usage: pf_matcher_probe windows.json left-index right-index | --all [params.json] | --batch pairs.json [params.json] | --actions | --grouping-directions results.json\n"; return 2; }
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
        w.directFaceModel=o["directFaceModel"].toString().toStdString();
        for(const auto& v:o["directFaces"].toArray()) {
            const auto f=v.toObject();pfcore::DirectFaceSample sample;sample.timestampSeconds=f["time"].toDouble();
            for(const auto& value:f["face"].toArray())sample.embedding.push_back(value.toDouble());
            w.directFaces.push_back(std::move(sample));
        }
        w.appearanceConfidence = o["bodyConfidence"].toDouble();
        for (const auto& v : o["face"].toArray()) w.faceEmbedding.push_back(v.toDouble());
        for (const auto& v : o["body"].toArray()) w.appearanceEmbedding.push_back(v.toDouble());
        for (const auto& v : o["context"].toArray()) w.sceneContext.push_back(v.toDouble());
        for (const auto& v : o["view"].toArray()) w.sceneView.push_back(v.toDouble());
        for (const auto& v : o["sequence"].toArray()) w.sceneSequence.push_back(v.toDouble());
        for (const auto& v : o["sequencePts"].toArray()) w.sceneSequenceTimes.push_back(v.toDouble());
        for (const auto& v:o["measuredFaces"].toArray()) {
            const auto f=v.toObject();w.measuredFaces.push_back({f["time"].toDouble(),f["observed"].toBool(),
                f["similarity"].toDouble(),f["eyeSpan"].toDouble()});
        }
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
    if(args[2]=="--grouping-directions") {
        if(args.size()!=4)return 2;
        QFile input(args[3]);if(!input.open(QIODevice::ReadOnly))return 2;
        const auto doc=QJsonDocument::fromJson(input.readAll(),&error);
        if(error.error!=QJsonParseError::NoError || !doc.isObject())return 2;
        const auto pairs=doc.object()["results"].toArray();
        const pfcore::MovementClassifier classifier;
        QJsonArray changes;
        for(qsizetype i=0;i<pairs.size();++i) {
            const auto p=pairs[i].toObject();
            const auto left=p["leftIndex"].toInteger(-1),right=p["rightIndex"].toInteger(-1);
            if(left<0 || right<0 || static_cast<std::size_t>(left)>=windows.size() || static_cast<std::size_t>(right)>=windows.size())return 2;
            const bool head=p["headOnlyComparison"].toBool();
            const auto a=classifier.classifyForGrouping(windows[left],p["leftStart"].toDouble(),p["leftEnd"].toDouble(),head);
            const auto b=classifier.classifyForGrouping(windows[right],p["rightStart"].toDouble(),p["rightEnd"].toDouble(),head);
            using D=pfcore::MovementDirection;using G=pfcore::GestureClass;
            const bool actionsKnown=a.gesture!=G::Unknown && b.gesture!=G::Unknown
                && std::isfinite(a.gestureConfidence) && std::isfinite(b.gestureConfidence)
                && a.gestureConfidence>=.65 && b.gestureConfidence>=.65;
            const D old=actionsKnown ? (a.direction==b.direction ? a.direction:D::Mixed):D::Unknown;
            const auto current=windows[left].staticFrameSet && windows[right].staticFrameSet
                ? D::Unknown : pfcore::MovementClassifier::combineForGrouping(a,b).direction;
            if(old!=current)changes.append(QJsonObject{{"index",i},{"leftIndex",left},{"rightIndex",right},
                {"leftStart",p["leftStart"]},{"rightStart",p["rightStart"]},
                {"old",pfcore::MovementClassifier::directionName(old)},
                {"current",pfcore::MovementClassifier::directionName(current)}});
        }
        std::cout<<QJsonDocument(QJsonObject{{"pairs",pairs.size()},{"changes",changes}}).toJson(QJsonDocument::Compact).constData()<<'\n';
        return 0;
    }
#ifdef PF_ORGANIZATION_PROBE
    if(args[2]=="--copied-scenes" || args[2]=="--scene-neighbours") {
        const bool neighbours=args[2]=="--scene-neighbours";
        const auto started=std::chrono::steady_clock::now();
        std::map<std::pair<std::string,std::size_t>,const pfcore::MotionWindow*> scenes;
        for(const auto& w:windows)if(w.hasSceneIndex&&!w.sceneSequence.empty())
            scenes.try_emplace(std::make_pair(w.sourceId,w.sceneIndex),&w);
        QJsonArray pairs;
        for(auto a=scenes.begin();a!=scenes.end();++a)for(auto b=std::next(a);b!=scenes.end();++b) {
            const auto& x=*a->second;const auto& y=*b->second;
            double difference=.06;
            if(neighbours)for(std::size_t i=0;i+2<x.sceneSequence.size()/432;++i)
                for(std::size_t j=0;j+2<y.sceneSequence.size()/432;++j) {
                    double error=0;
                    for(std::size_t p=0;p<1296;++p) {
                        error+=std::abs(x.sceneSequence[i*432+p]-y.sceneSequence[j*432+p]);
                        if(error>difference*1296)break;
                    }
                    difference=std::min(difference,error/1296);
                }
            if((neighbours&&difference<.06)||(!neighbours&&pfcore::MotionMatcher::sameFootageRange(x,x.sceneStartSeconds,x.sceneEndSeconds,
                y,y.sceneStartSeconds,y.sceneEndSeconds)))
                pairs.append(QJsonObject{{"leftScene",static_cast<qint64>(x.sceneIndex)},
                    {"rightScene",static_cast<qint64>(y.sceneIndex)},
                    {"leftStart",x.sceneStartSeconds},{"leftEnd",x.sceneEndSeconds},
                    {"rightStart",y.sceneStartSeconds},{"rightEnd",y.sceneEndSeconds},{"sampleDifference",difference}});
        }
        const QJsonObject report{{"scope","measured sampled pixels; candidate copies require manual review; not identity ground truth"},
            {"sceneCount",static_cast<qint64>(scenes.size())},{"pairs",pairs},
            {"elapsedMs",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()}};
        std::cout<<QJsonDocument(report).toJson(QJsonDocument::Compact).constData()<<"\n";return 0;
    }
#endif
    if (args[2]=="--actions" || args[2]=="--labelled-actions") {
        const pfcore::MovementClassifier classifier;
        const bool labelled=args[2]=="--labelled-actions";
        const auto started=std::chrono::steady_clock::now();
        QJsonArray rows;
        for(std::size_t index=0;index<windows.size();++index) {
            const auto& window=windows[index];
            if(window.frames.empty())return 2;
            const auto start=window.frames.front().timestampSeconds;
            const auto metadata=json.array()[static_cast<qsizetype>(index)].toObject();
            const auto end=window.frames.back().timestampSeconds+(labelled && window.frames.size()>1
                ? window.frames.back().timestampSeconds-window.frames[window.frames.size()-2].timestampSeconds:0);
            const bool head=labelled && metadata["headOnly"].toBool();
            const auto classification=classifier.classifyForGrouping(window,start,end,head);
            const auto predicted=labelled && metadata["pose"].toBool()
                ? classifier.classifyPoseForGrouping(window,start,end,head)
                : std::string(pfcore::MovementClassifier::gestureName(classification.gesture));
            QJsonArray actions;
            for(const auto action:classification.actions)
                actions.append(pfcore::MovementClassifier::gestureName(action));
            rows.append(QJsonObject{{"index",static_cast<qint64>(index)},
                {"source",QString::fromStdString(window.sourceId)},
                {"start",start},{"end",end},
                {"gesture",QString::fromStdString(predicted)},{"expected",labelled?metadata["expected"]:QJsonValue()},
                {"caseId",labelled?metadata["name"]:QJsonValue()},
                {"actions",actions},{"confidence",classification.gestureConfidence}});
        }
        std::cout<<QJsonDocument(QJsonObject{{"scope","grouping only; no identity or matcher admission"},
            {"results",rows},{"elapsedMs",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()},
            {"labelled",labelled}}).toJson(QJsonDocument::Compact).constData()<<'\n';
        return 0;
    }
    if (args[2]=="--identity") {
        const auto selected=pfcore::selectDominantVideoWindows(windows);
        QJsonArray sources,mask;
        for (const auto& source:selected.sources)sources.append(QString::fromStdString(source));
        for (const auto yes:selected.windows)mask.append(yes);
        const auto bytes=QJsonDocument(QJsonObject{{"sources",sources},{"windows",mask},
            {"supplied",static_cast<qint64>(selected.suppliedSources)}}).toJson(QJsonDocument::Compact);
        std::cout<<bytes.constData()<<'\n';return 0;
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
    QJsonObject configuration;
    if (args.size() > configIndex) {
        QFile config(args[configIndex]);
        if (!config.open(QIODevice::ReadOnly)) return 2;
        const auto doc = QJsonDocument::fromJson(config.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) return 2;
        const auto c = doc.object();
        configuration=c;
#define PF_NUMBER(name) if (c.contains(#name)) params.name = c[#name].toDouble()
#define PF_BOOL(name) if (c.contains(#name)) params.name = c[#name].toBool()
        PF_NUMBER(similarityThreshold); PF_NUMBER(candidateThreshold); PF_NUMBER(minRepeatGapSec);
        PF_NUMBER(sameFileGapSec); PF_NUMBER(crossFileGapSec); PF_NUMBER(duplicateWindowSec);
        PF_NUMBER(noiseFactor); PF_NUMBER(timeWeight); PF_NUMBER(maxUniqueResults);
        PF_NUMBER(maxResultsPerShot); PF_NUMBER(minTemporalFrames); PF_NUMBER(minMotionSpanSec);
        PF_NUMBER(coverageSeedLimit);
        PF_NUMBER(minimumResultDurationSec);
        PF_NUMBER(maxComparisonThreads);
        PF_NUMBER(staticPoseSimilarityThreshold); PF_NUMBER(staticArticulationSimilarityThreshold);
        PF_NUMBER(minAppearanceSimilarity); PF_NUMBER(minFaceSimilarity); PF_NUMBER(sameSceneContextThreshold);
        PF_NUMBER(minMotionBodyIdentitySimilarity);
        PF_NUMBER(minHeadFaceSimilarity);
        PF_NUMBER(sameSourceGapFloorSec); PF_NUMBER(minMotionRange); PF_NUMBER(motionDeltaThreshold);
        PF_NUMBER(dtwBand); PF_NUMBER(sakoeChibaRatio); PF_NUMBER(minActiveTransitionRatio);
        PF_NUMBER(temporalSimilarityThreshold); PF_NUMBER(minTemporalDurationSec);
        PF_NUMBER(nmsOverlapThreshold); PF_NUMBER(appearanceWeight); PF_NUMBER(minAppearanceEvidence);
        PF_NUMBER(sameSceneContextGapSec);
        PF_BOOL(requireAppearance); PF_BOOL(requireSameTrackWithinSource); PF_BOOL(mirrorInvariant);
        PF_BOOL(auditDirectFaceConsensus);
        PF_BOOL(normalizeSize); PF_BOOL(expandedSearch); PF_BOOL(allowStaticFrames);
        PF_BOOL(individualPairs);
        PF_BOOL(recoverUnusedShots);
        PF_BOOL(bodyMotionOnly);
        PF_BOOL(bodyScaleInvariant);
        PF_BOOL(reuseCameraConflicts); PF_BOOL(reuseRejectedComparisons);
#undef PF_NUMBER
#undef PF_BOOL
    }
    const auto resultJson = [](const pfcore::MotionMatch& m) {
        QJsonObject result{{"leftIndex", static_cast<qint64>(m.leftIndex)},
            {"rightIndex", static_cast<qint64>(m.rightIndex)},
            {"leftSource",QString::fromStdString(m.leftSourceId)},
            {"rightSource",QString::fromStdString(m.rightSourceId)},
            {"leftStart",m.leftStartSeconds},{"leftEnd",m.leftEndSeconds},
            {"rightStart",m.rightStartSeconds},{"rightEnd",m.rightEndSeconds},
            {"similarity",m.similarity},{"dtwDistance",m.dtwDistance},
            {"identityVerified",m.appearanceVerified},{"faceVerified",m.faceVerified},
            {"appearanceSimilarity",m.appearanceSimilarity},{"sceneSimilarity",m.sceneSimilarity},
            {"headOnlyComparison",m.headOnlyComparison},{"rankScore",m.rankScore}};
#ifdef PF_ORGANIZATION_PROBE
        result["leftSceneFamily"]=QString::fromStdString(m.leftSceneFamily);
        result["rightSceneFamily"]=QString::fromStdString(m.rightSceneFamily);
        result["evidenceQuality"]=m.evidenceQuality;result["bestMatchScore"]=m.bestMatchScore;
        result["continuityRank"]=static_cast<qint64>(m.continuityRank);
#endif
        return result;
    };
    if (all) {
        const int passes=std::clamp(configuration.value("reusePasses").toInt(1),1,10);
        pfcore::MotionSearchReuse reuse(256U*1024U*1024U);
        pfcore::MotionSearchControl control;if(passes>1)control.reuse=&reuse;
        const auto start = std::chrono::steady_clock::now();
        std::vector<pfcore::MotionMatch> matches;
        QJsonArray passTimes;
        for(int pass=0;pass<passes;++pass) {
            const auto passStart=std::chrono::steady_clock::now();
            matches=pfcore::MotionMatcher(params).findAllPairs(windows,control);
            passTimes.append(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-passStart).count());
        }
        const auto matcherMs = std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        pfcore::MotionRanker::rank(matches,windows);
#ifdef PF_ORGANIZATION_PROBE
        const auto organizationStart=std::chrono::steady_clock::now();
        QJsonArray organizationPassTimes;
        if(configuration.value("organizeResults").toBool()) {
            const auto raw=matches;
            const int organizationPasses=std::clamp(configuration.value("organizationPasses").toInt(1),1,100);
            for(int pass=0;pass<organizationPasses;++pass) {
                matches=raw;
                const auto started=std::chrono::steady_clock::now();
                pfcore::MotionRanker::organize(matches,windows);
                organizationPassTimes.append(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count());
            }
        }
        const auto organizationMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-organizationStart).count();
#endif
        QJsonArray results, inputs;
        std::set<std::string> sources;
        for (const auto& w : windows) sources.insert(w.sourceId);
        for (const auto& s : sources) inputs.append(QString::fromStdString(s));
        for (const auto& match : matches) results.append(resultJson(match));
        QJsonObject report{{"inputVideos",inputs},{"results",results},{"elapsedMs",matcherMs},
            {"passTimesMs",passTimes},
            {"matcherOnly",true},{"windowCount",static_cast<qint64>(windows.size())}};
#ifdef PF_ORGANIZATION_PROBE
        report["organizationMs"]=organizationMs;
        report["organizationPassTimesMs"]=organizationPassTimes;
        report["organizedResults"]=configuration.value("organizeResults").toBool();
#endif
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
            const auto match = matcher.compare(windows[a],windows[b],a,b);
            auto result = resultJson(match);
            result["comparisonRejection"] = rejectionName(match.rejection);
            result["rejectionScope"] = "first failing gate of the winning comparison variant; excludes retrieval and selection";
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
              << " headOnly=" << match.headOnlyComparison
              << " rejection=" << rejectionName(match.rejection) << "\n";
}
