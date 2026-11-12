#include <pfcore/MovementClassifier.hpp>
#include <pfcore/VideoActionEvidence.hpp>
#include <pfcore/SceneWindow.hpp>
#include <pfservices/MotionWindowCache.hpp>
#include <pfservices/CharacterAnalysisCache.hpp>
#include <QFile>
#include <QSaveFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <fstream>
#include <iostream>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <filesystem>
#include <array>

namespace {
double prototypeCosine(const std::vector<float>& a,const std::vector<float>& b) {
    if(a.empty()||a.size()!=b.size())return -1.;
    double dot=0,aa=0,bb=0;
    for(std::size_t i=0;i<a.size();++i) {
        const double x=a[i],y=b[i];dot+=x*y;aa+=x*x;bb+=y*y;
    }
    return aa>0&&bb>0?dot/std::sqrt(aa*bb):-1.;
}

int characterPairReport(const pfservices::CharacterAnalysis& analysis,const QStringList& arguments) {
    bool leftValid=false,rightValid=false;
    const auto leftId=arguments[4].toULongLong(&leftValid),rightId=arguments[5].toULongLong(&rightValid);
    if(!leftValid||!rightValid||leftId==rightId)return 2;
    const auto find=[&](qulonglong id) {
        return std::find_if(analysis.candidates.begin(),analysis.candidates.end(),[&](const auto& candidate) {return candidate.id==id;});
    };
    const auto left=find(leftId),right=find(rightId);
    if(left==analysis.candidates.end()||right==analysis.candidates.end())return 8;
    const auto details=[](const pfcore::CharacterCandidate& c) {
        return QJsonObject{{"id",static_cast<qint64>(c.id)},{"tracks",static_cast<qint64>(c.trackIds.size())},
            {"observations",static_cast<qint64>(c.observationCount)},{"presenceSeconds",c.presenceSeconds},
            {"distinctIdentity",c.distinctIdentity},{"faceEvidence",c.prototype.faceEvidence},{"bodyEvidence",c.prototype.bodyEvidence}};
    };
    auto a=left->prototype.observationTimes,b=right->prototype.observationTimes;
    std::sort(a.begin(),a.end());std::sort(b.begin(),b.end());
    QJsonArray times;std::size_t shared=0,i=0,j=0;
    while(i<a.size()&&j<b.size()) {
        if(std::abs(a[i]-b[j])<=1e-6) {
            if(times.size()<32)times.append(a[i]);
            ++shared;++i;++j;
        } else if(a[i]<b[j])++i;
        else ++j;
    }
    const std::set<std::size_t> leftTracks(left->trackIds.begin(),left->trackIds.end());
    const std::set<std::size_t> rightTracks(right->trackIds.begin(),right->trackIds.end());
    struct Sample { const pfcore::PoseFrame* frame=nullptr; std::size_t track=0; };
    std::map<double,std::array<Sample,2>> windowSamples;
    for(const auto& window:analysis.windows) {
        const int side=leftTracks.contains(window.trackId)?0:rightTracks.contains(window.trackId)?1:-1;
        if(side<0)continue;
        for(const auto& frame:window.frames)if(std::isfinite(frame.timestampSeconds)&&!frame.keypoints.empty())
            windowSamples[frame.timestampSeconds][side]={&frame,window.trackId};
    }
    QJsonArray pairedWindowSamples;std::size_t pairedWindowCount=0;
    for(const auto& [time,pair]:windowSamples) {
        if(!pair[0].frame||!pair[1].frame)continue;
        ++pairedWindowCount;
        if(pairedWindowSamples.size()>=32)continue;
        const auto nose=[](const Sample& sample) {
            const auto& p=sample.frame->keypoints.front();
            return QJsonObject{{"track",static_cast<qint64>(sample.track)},{"x",p.x},{"y",p.y},{"confidence",p.confidence}};
        };
        pairedWindowSamples.append(QJsonObject{{"time",time},{"left",nose(pair[0])},{"right",nose(pair[1])}});
    }
    QSaveFile output(arguments[3]);
    if(QFile::exists(arguments[3])||!output.open(QIODevice::WriteOnly))return 6;
    const auto bytes=QJsonDocument(QJsonObject{{"scope","Cached candidates before repeated-scene merge; shared sample times are not a person-conflict verdict"},
        {"left",details(*left)},{"right",details(*right)},
        {"facePrototypeCosine",prototypeCosine(left->prototype.face,right->prototype.face)},
        {"bodyPrototypeCosine",prototypeCosine(left->prototype.body,right->prototype.body)},
        {"sharedPrototypeSampleTimes",static_cast<qint64>(shared)},{"firstSharedPrototypeTimes",times},
        {"sharedWindowSampleTimes",static_cast<qint64>(pairedWindowCount)},{"firstSharedWindowSamples",pairedWindowSamples}}).toJson();
    return output.write(bytes)==bytes.size()&&output.commit()?0:6;
}

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
        if(object["schema"].toInt()!=2 || !object["source"].isString() || !object["model"].isString()
            || !object["start"].isDouble() || !object["end"].isDouble())continue;
        pfcore::VideoActionEvidence evidence;
        evidence.sourceId=object["source"].toString().toStdString();
        evidence.modelHash=object["model"].toString().toStdString();
        evidence.start=object["start"].toDouble();evidence.end=object["end"].toDouble();
        if(evidence.sourceId.empty() || !std::isfinite(evidence.start) || !std::isfinite(evidence.end)
            || evidence.end<=evidence.start)continue;
        bool valid=true;
        for(const auto field:{"observed","continuous","associated","torsoVisible","postureVisible"}) {
            if(!object[field].isBool()){valid=false;break;}
        }
        if(!valid || !object["times"].isArray() || !object["scores"].isArray() || !object["frozen"].isArray())continue;
        evidence.observed=object["observed"].toBool();evidence.continuous=object["continuous"].toBool();
        evidence.associated=object["associated"].toBool();evidence.torsoVisible=object["torsoVisible"].toBool();
        evidence.postureVisible=object["postureVisible"].toBool();
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
    if(argc<4 || argc>6) {std::cerr<<"Usage: pf_grouping_probe MOTION.pfc RESULTS.json REVIEW.json [VERIFIED-CUTS.json|VIDEO-ACTION-CACHE] [VIDEO-ACTION-CACHE]\n"
        <<"   or: pf_grouping_probe CHARACTER.pfc --character-pair REVIEW.json LEFT_ID RIGHT_ID\n";return 2;}
    const bool characterPair=argc==6&&std::string_view(argv[2])=="--character-pair";
    std::filesystem::path cutsPath,actionCachePath;
    if(!characterPair&&argc>=5) {
        std::error_code error;
        if(std::filesystem::is_directory(argv[4],error) && !error)actionCachePath=argv[4];
        else cutsPath=argv[4];
    }
    if(!characterPair&&argc==6)actionCachePath=argv[5];
    const auto videoActions=actionCachePath.empty()?std::vector<pfcore::VideoActionEvidence>{}
        :readVideoActionCache(actionCachePath);
    std::ifstream input(argv[1],std::ios::binary);char magic[8];std::uint32_t version=0,keySize=0;std::uint64_t size=0;
    input.read(magic,8);input.read(reinterpret_cast<char*>(&version),4);
    input.read(reinterpret_cast<char*>(&keySize),4);input.read(reinterpret_cast<char*>(&size),8);
    if(!input||std::string(magic,8)!="PFCACHE1"||version!=1||keySize>16*1024||size>512ULL*1024*1024)return 3;
    input.seekg(0,std::ios::end);
    if(input.tellg()!=static_cast<std::streamoff>(24ULL+keySize+size))return 3;
    input.seekg(24ULL+keySize,std::ios::beg);std::vector<std::uint8_t> bytes(size);
    input.read(reinterpret_cast<char*>(bytes.data()),size);if(!input)return 3;
    std::vector<pfcore::MotionWindow> windows;int scenes=0;
    const bool characterArtifact=bytes.size()>=4 && std::equal(bytes.begin(),bytes.begin()+4,"PFCH");
    if(characterArtifact) {
        pfservices::CharacterAnalysis analysis;
        if(!pfservices::deserializeCharacterAnalysis(bytes,analysis))return 4;
        if(characterPair) {
            QStringList arguments;for(int i=0;i<argc;++i)arguments.append(QString::fromLocal8Bit(argv[i]));
            return characterPairReport(analysis,arguments);
        }
        windows=std::move(analysis.windows);scenes=analysis.sceneCount;
    }else if(characterPair||!pfservices::deserializeMotionWindows(bytes,windows,scenes))return 4;
    QFile results(argv[2]);if(!results.open(QIODevice::ReadOnly))return 5;
    auto report=QJsonDocument::fromJson(results.readAll()).object();QJsonArray reviewed;
    report["observationArtifact"]=characterArtifact?"PFCH":"motion-windows";
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
                row[side+"Posture"]=QString::fromStdString(
                    classifier.classifyPostureForGrouping(*best,reviewStart,reviewEnd));
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
            const auto leftPosture=row["leftPosture"].toString(),rightPosture=row["rightPosture"].toString();
            if(leftPosture=="seated_lying"&&rightPosture=="seated_lying")row["groupedPosture"]="seated_lying";
            else if(leftPosture=="standing"&&rightPosture=="standing")row["groupedPosture"]="standing";
            else if(leftPosture!="unknown"&&rightPosture!="unknown")row["groupedPosture"]="posture_mismatch";
            else if(leftPosture!="unknown"||rightPosture!="unknown")row["groupedPosture"]="posture_uncertain";
            else row["groupedPosture"]="unknown";
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
            {"windowIndex",static_cast<qint64>(&w-windows.data())},
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
    QJsonArray staticWindows;
    for(std::size_t i=0;i<windows.size();++i) {
        const auto& window=windows[i];
        if(!window.staticFrameSet || window.frames.empty())continue;
        staticWindows.append(QJsonObject{{"windowIndex",static_cast<qint64>(i)},
            {"track",static_cast<qint64>(window.trackId)},
            {"source",QString::fromStdString(window.sourceId)},
            {"scene",static_cast<qint64>(window.sceneIndex)},
            {"start",window.frames.front().timestampSeconds},{"end",window.frames.back().timestampSeconds}});
    }
    report["staticWindows"]=staticWindows;
    QFile output(argv[3]);if(!output.open(QIODevice::WriteOnly|QIODevice::NewOnly))return 6;
    output.write(QJsonDocument(report).toJson());std::cout<<"Reviewed "<<reviewed.size()<<" pairs from "<<windows.size()<<" cached windows\n";
}
