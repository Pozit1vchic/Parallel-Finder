#include <pfcore/MovementClassifier.hpp>
#include <pfcore/VideoActionEvidence.hpp>
#include <pfcore/SceneWindow.hpp>
#include <pfservices/MotionWindowCache.hpp>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <map>
#include <filesystem>
#include <array>

namespace {
std::vector<pfcore::VideoActionEvidence> readVideoActionCache(const std::filesystem::path& root) {
    std::vector<pfcore::VideoActionEvidence> result;
    std::error_code error;
    if(!std::filesystem::is_directory(root,error) || error)return result;
    for(const auto& entry:std::filesystem::directory_iterator(root,error)) {
        if(error)break;
        if(entry.path().extension()!=".pfc" || entry.is_symlink(error) || !entry.is_regular_file(error))continue;
        std::ifstream input(entry.path(),std::ios::binary);
        char magic[8];std::uint32_t version=0,keySize=0;std::uint64_t size=0;
        input.read(magic,8);input.read(reinterpret_cast<char*>(&version),4);
        input.read(reinterpret_cast<char*>(&keySize),4);input.read(reinterpret_cast<char*>(&size),8);
        if(!input || std::string(magic,8)!="PFCACHE1" || version!=1 || keySize>16*1024 || size>1024*1024)continue;
        input.seekg(keySize,std::ios::cur);
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
        input.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));
        if(!input)continue;
        const auto document=QJsonDocument::fromJson(QByteArray(reinterpret_cast<const char*>(bytes.data()),
            static_cast<qsizetype>(bytes.size())));
        if(!document.isObject())continue;
        const auto object=document.object();
        if(object["schema"].toInt()!=1 || !object["source"].isString() || !object["model"].isString()
            || !object["start"].isDouble() || !object["end"].isDouble())continue;
        pfcore::VideoActionEvidence evidence;
        evidence.sourceId=object["source"].toString().toStdString();
        evidence.modelHash=object["model"].toString().toStdString();
        evidence.start=object["start"].toDouble();evidence.end=object["end"].toDouble();
        if(evidence.sourceId.empty() || !std::isfinite(evidence.start) || !std::isfinite(evidence.end)
            || evidence.end<=evidence.start)continue;
        bool valid=true;
        for(const auto field:{"observed","continuous","associated","torsoVisible"}) {
            if(!object[field].isBool()){valid=false;break;}
        }
        if(!valid || !object["times"].isArray() || !object["scores"].isArray() || !object["frozen"].isArray())continue;
        evidence.observed=object["observed"].toBool();evidence.continuous=object["continuous"].toBool();
        evidence.associated=object["associated"].toBool();evidence.torsoVisible=object["torsoVisible"].toBool();
        for(const auto value:object["times"].toArray()) {
            if(!value.isDouble() || !std::isfinite(value.toDouble())){valid=false;break;}
            evidence.frameTimes.push_back(value.toDouble());
        }
        if(!valid || evidence.frameTimes.size()>192)continue;
        if(evidence.observed) {
            const auto scores=object["scores"].toArray(),frozen=object["frozen"].toArray();
            if(scores.size()!=80 || frozen.size()!=80){continue;}
            for(int i=0;i<80;++i) {
                if(!scores[i].isDouble() || !frozen[i].isDouble()){valid=false;break;}
                evidence.scores[i]=scores[i].toDouble();evidence.frozenScores[i]=frozen[i].toDouble();
                if(!std::isfinite(evidence.scores[i]) || !std::isfinite(evidence.frozenScores[i])
                    || evidence.scores[i]<0 || evidence.scores[i]>1
                    || evidence.frozenScores[i]<0 || evidence.frozenScores[i]>1){valid=false;break;}
            }
        }
        if(valid)result.push_back(std::move(evidence));
    }
    return result;
}
}

// Read-only replay of existing observations and cut boundaries. No model,
// decoder, matcher, cache writes or eviction runs in this review tool.
int main(int argc,char** argv) {
    if(argc<4 || argc>6) {std::cerr<<"Usage: pf_grouping_probe MOTION.pfc RESULTS.json REVIEW.json [VERIFIED-CUTS.json|VIDEO-ACTION-CACHE] [VIDEO-ACTION-CACHE]\n";return 2;}
    std::filesystem::path cutsPath,actionCachePath;
    if(argc>=5) {
        std::error_code error;
        if(std::filesystem::is_directory(argv[4],error) && !error)actionCachePath=argv[4];
        else cutsPath=argv[4];
    }
    if(argc==6)actionCachePath=argv[5];
    const auto videoActions=actionCachePath.empty()?std::vector<pfcore::VideoActionEvidence>{}
        :readVideoActionCache(actionCachePath);
    std::ifstream input(argv[1],std::ios::binary);char magic[8];std::uint32_t version=0,keySize=0;std::uint64_t size=0;
    input.read(magic,8);input.read(reinterpret_cast<char*>(&version),4);
    input.read(reinterpret_cast<char*>(&keySize),4);input.read(reinterpret_cast<char*>(&size),8);
    if(!input||std::string(magic,8)!="PFCACHE1"||version!=1||keySize>16*1024||size>512ULL*1024*1024)return 3;
    input.seekg(keySize,std::ios::cur);std::vector<std::uint8_t> bytes(size);
    input.read(reinterpret_cast<char*>(bytes.data()),size);if(!input)return 3;
    std::vector<pfcore::MotionWindow> windows;int scenes=0;
    if(!pfservices::deserializeMotionWindows(bytes,windows,scenes))return 4;
    QFile results(argv[2]);if(!results.open(QIODevice::ReadOnly))return 5;
    auto report=QJsonDocument::fromJson(results.readAll()).object();QJsonArray reviewed;
    if(!cutsPath.empty()) {
        QFile cutsFile(QString::fromStdString(cutsPath.string()));if(!cutsFile.open(QIODevice::ReadOnly))return 7;
        const auto cutsReport=QJsonDocument::fromJson(cutsFile.readAll()).object();
        std::vector<double> cuts;
        for(const auto value:cutsReport["boundaries"].toArray()) {
            const auto time=value.toObject()["time"].toDouble(-1);
            if(!std::isfinite(time)||time<0)return 7;
            cuts.push_back(time);
        }
        std::sort(cuts.begin(),cuts.end());cuts.erase(std::unique(cuts.begin(),cuts.end()),cuts.end());
        std::size_t refresh=0;
        std::vector<pfcore::MotionWindow> splitWindows;
        for(auto& part:pfcore::splitSourceWindowsAtCuts(windows,cuts)) {
            refresh+=part.identityNeedsRefresh;
            splitWindows.push_back(std::move(part.window));
        }
        windows=std::move(splitWindows);
        report["diagnosticContinuityReplay"]=true;
        report["cutSource"]=cutsReport["source"];
        report["identityRefreshRequiredWindows"]=static_cast<qint64>(refresh);
        // This diagnostic can replay poses from a moved source. It does NOT
        // prove complete source-byte identity or admit these parts as matches.
    }
    const pfcore::MovementClassifier classifier;
    for(const auto item:report["results"].toArray()) {
        auto row=item.toObject();
        std::array<pfcore::MovementClassification,2> sideGroups{};
        std::array<bool,2> hasSideGroup{};
        for(const QString& side:{QString("left"),QString("right")}) {
            const std::size_t sideIndex=side=="left"?0:1;
            const double start=row[side+"Start"].toDouble(),end=row[side+"End"].toDouble();
            const auto track=row[side+"TrackId"].toInteger();
            const auto source=row[side+"Source"].toString().toStdString();
            const bool pose=row["matchType"].toString()=="pose",head=row["headOnlyComparison"].toBool();
            pfcore::MotionWindow* best=nullptr;std::size_t supported=0;
            for(auto& w:windows)if(w.sourceId==source && w.trackId==static_cast<std::size_t>(track)&&w.staticFrameSet==pose) {
                std::size_t count=0;
                for(const auto& f:w.frames)count+=f.timestampSeconds>=start-1e-6&&f.timestampSeconds<=end+1e-6;
                if(count>supported) {supported=count;best=&w;}
            }
            row[side+"Samples"]=static_cast<int>(supported);
            if(best) {
                const double reviewStart=!cutsPath.empty() ? std::max(start,best->sceneStartSeconds):start;
                const double reviewEnd=!cutsPath.empty() ? std::min(end,best->sceneEndSeconds):end;
                if(!cutsPath.empty()) {
                    row[side+"ContinuityStart"]=reviewStart;
                    row[side+"ContinuityEnd"]=reviewEnd;
                }
                for(const auto& action:videoActions)
                    if(action.sourceId==best->sourceId && std::abs(action.start-reviewStart)<=1e-6
                        && std::abs(action.end-reviewEnd)<=1e-6)
                        best->videoActions.push_back(action);
                const auto group=classifier.classifyForGrouping(*best,reviewStart,reviewEnd,head);
                sideGroups[sideIndex]=group;hasSideGroup[sideIndex]=true;
                QJsonArray actions;for(const auto action:group.actions)actions.append(pfcore::MovementClassifier::gestureName(action));
                row[side+"Actions"]=actions;
                row[side+"Gesture"]=pfcore::MovementClassifier::gestureName(group.gesture);
                row[side+"Direction"]=pfcore::MovementClassifier::directionName(group.direction);
                row[side+"GestureConfidence"]=group.gestureConfidence;
                row[side+"DirectionConfidence"]=group.directionConfidence;
                row[side+"Group"]=QString::fromStdString(pose ? classifier.classifyPoseForGrouping(*best,reviewStart,reviewEnd,head)
                    : pfcore::MovementClassifier::gestureName(group.gesture));
                QJsonArray frames;
                for(const auto& f:best->frames)if(f.timestampSeconds>=start-1e-6&&f.timestampSeconds<=end+1e-6) {
                    QJsonArray points;for(const auto& p:f.keypoints)points.append(QJsonArray{p.x,p.y,p.confidence});
                    frames.append(QJsonObject{{"time",f.timestampSeconds},{"points",points}});
                }
                row[side+"Frames"]=frames;
            }
        }
        if(hasSideGroup[0] && hasSideGroup[1]) {
            const auto combined=pfcore::MovementClassifier::combineForGrouping(sideGroups[0],sideGroups[1]);
            row["groupedGesture"]=pfcore::MovementClassifier::gestureName(combined.gesture);
            row["groupedDirection"]=pfcore::MovementClassifier::directionName(combined.direction);
            QJsonArray sharedActions;
            for(const auto action:combined.actions)sharedActions.append(pfcore::MovementClassifier::gestureName(action));
            row["groupedActions"]=sharedActions;
        }
        reviewed.append(row);
    }
    report["results"]=reviewed;report["cachedWindows"]=static_cast<int>(windows.size());
    report["loadedVideoActionEvidence"]=static_cast<int>(videoActions.size());
    QJsonArray candidates;
    for(const auto& w:windows)if(!w.staticFrameSet && w.frames.size()>=5) {
        const auto start=w.frames.front().timestampSeconds,end=w.frames.back().timestampSeconds;
        const auto body=classifier.classifyForGrouping(w,start,end),head=classifier.classifyForGrouping(w,start,end,true);
        const auto displacement=classifier.classify(w,start,end);
        QJsonArray actions;for(const auto action:body.actions)actions.append(pfcore::MovementClassifier::gestureName(action));
        candidates.append(QJsonObject{{"source",QString::fromStdString(w.sourceId)},
            {"scene",static_cast<qint64>(w.sceneIndex)},{"start",start},{"end",end},
            {"track",static_cast<qint64>(w.trackId)},
            {"actions",actions},
            {"body",pfcore::MovementClassifier::gestureName(body.gesture)},
            {"head",pfcore::MovementClassifier::gestureName(head.gesture)},
            {"direction",pfcore::MovementClassifier::directionName(displacement.direction)},
            {"directionConfidence",displacement.directionConfidence},
            {"confidence",body.gestureConfidence},{"headConfidence",head.gestureConfidence}});
        if(body.gesture==pfcore::GestureClass::Step
            || std::find(body.actions.begin(),body.actions.end(),pfcore::GestureClass::Step)!=body.actions.end()) {
            QJsonArray gaitFrames;
            for(const auto& frame:w.frames)if(frame.timestampSeconds>=start-1e-6&&frame.timestampSeconds<=end+1e-6) {
                QJsonArray points;for(const auto& point:frame.keypoints)points.append(QJsonArray{point.x,point.y,point.confidence});
                gaitFrames.append(QJsonObject{{"time",frame.timestampSeconds},{"points",points}});
            }
            auto candidate=candidates[candidates.size()-1].toObject();
            candidate["gaitFrames"]=gaitFrames;
            candidates[candidates.size()-1]=candidate;
        }
    }
    report["candidateWindows"]=candidates;
    QFile output(argv[3]);if(!output.open(QIODevice::WriteOnly|QIODevice::NewOnly))return 6;
    output.write(QJsonDocument(report).toJson());std::cout<<"Reviewed "<<reviewed.size()<<" pairs from "<<windows.size()<<" cached windows\n";
}
