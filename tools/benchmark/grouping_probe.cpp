#include <pfcore/MovementClassifier.hpp>
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

// Read-only replay of existing observations and cut boundaries. No model,
// decoder, matcher, cache writes or eviction runs in this review tool.
int main(int argc,char** argv) {
    if(argc!=4 && argc!=5) {std::cerr<<"Usage: pf_grouping_probe MOTION.pfc RESULTS.json REVIEW.json [VERIFIED-CUTS.json]\n";return 2;}
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
    if(argc==5) {
        QFile cutsFile(argv[4]);if(!cutsFile.open(QIODevice::ReadOnly))return 7;
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
        for(const QString& side:{QString("left"),QString("right")}) {
            const double start=row[side+"Start"].toDouble(),end=row[side+"End"].toDouble();
            const auto track=row[side+"TrackId"].toInteger();
            const bool pose=row["matchType"].toString()=="pose",head=row["headOnlyComparison"].toBool();
            const pfcore::MotionWindow* best=nullptr;std::size_t supported=0;
            for(const auto& w:windows)if(w.trackId==static_cast<std::size_t>(track)&&w.staticFrameSet==pose) {
                std::size_t count=0;
                for(const auto& f:w.frames)count+=f.timestampSeconds>=start-1e-6&&f.timestampSeconds<=end+1e-6;
                if(count>supported) {supported=count;best=&w;}
            }
            row[side+"Samples"]=static_cast<int>(supported);
            if(best) {
                const double reviewStart=argc==5 ? std::max(start,best->sceneStartSeconds):start;
                const double reviewEnd=argc==5 ? std::min(end,best->sceneEndSeconds):end;
                if(argc==5) {
                    row[side+"ContinuityStart"]=reviewStart;
                    row[side+"ContinuityEnd"]=reviewEnd;
                }
                const auto group=classifier.classifyForGrouping(*best,reviewStart,reviewEnd,head);
                QJsonArray actions;for(const auto action:group.actions)actions.append(pfcore::MovementClassifier::gestureName(action));
                row[side+"Actions"]=actions;
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
        reviewed.append(row);
    }
    report["results"]=reviewed;report["cachedWindows"]=static_cast<int>(windows.size());
    QJsonArray candidates;
    for(const auto& w:windows)if(!w.staticFrameSet && w.frames.size()>=5) {
        const auto start=w.frames.front().timestampSeconds,end=w.frames.back().timestampSeconds;
        const auto body=classifier.classifyForGrouping(w,start,end),head=classifier.classifyForGrouping(w,start,end,true);
        QJsonArray actions;for(const auto action:body.actions)actions.append(pfcore::MovementClassifier::gestureName(action));
        candidates.append(QJsonObject{{"start",start},{"end",end},{"track",static_cast<qint64>(w.trackId)},
            {"actions",actions},
            {"body",pfcore::MovementClassifier::gestureName(body.gesture)},
            {"head",pfcore::MovementClassifier::gestureName(head.gesture)},
            {"confidence",body.gestureConfidence},{"headConfidence",head.gestureConfidence}});
    }
    report["candidateWindows"]=candidates;
    QFile output(argv[3]);if(!output.open(QIODevice::WriteOnly|QIODevice::NewOnly))return 6;
    output.write(QJsonDocument(report).toJson());std::cout<<"Reviewed "<<reviewed.size()<<" pairs from "<<windows.size()<<" cached windows\n";
}
