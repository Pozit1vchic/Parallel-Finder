#include <pfcore/SceneDetector.hpp>
#include <pfcore/SceneSequenceCache.hpp>
#include <pfservices/MotionWindowCache.hpp>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <fstream>
#include <iostream>
#include <map>

// Read a COPY of derived observations. No source decoding or cache writes.
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);const auto args=app.arguments();
    if(args.size()!=3 && args.size()!=4){std::cerr<<"Usage: pf_scene_candidates_probe SEQUENCES.pfc OUTPUT.json [MOTION.pfc]\n";return 2;}
    std::ifstream input(args[1].toStdString(),std::ios::binary);
    char magic[8];std::uint32_t version=0,keySize=0;std::uint64_t size=0;
    input.read(magic,8);input.read(reinterpret_cast<char*>(&version),4);
    input.read(reinterpret_cast<char*>(&keySize),4);input.read(reinterpret_cast<char*>(&size),8);
    if(!input || std::string(magic,8)!="PFCACHE1" || version!=1 || keySize>8192 || size>512ULL*1024*1024)return 3;
    input.seekg(keySize,std::ios::cur);std::vector<std::uint8_t> bytes(size);
    input.read(reinterpret_cast<char*>(bytes.data()),size);if(!input)return 3;
    const auto scenes=pfcore::decodeSceneSequences(bytes);if(!scenes)return 4;
    std::map<std::pair<double,double>,double> lastPoses;
    if(args.size()==4) {
        std::ifstream motion(args[3].toStdString(),std::ios::binary);
        motion.read(magic,8);motion.read(reinterpret_cast<char*>(&version),4);
        motion.read(reinterpret_cast<char*>(&keySize),4);motion.read(reinterpret_cast<char*>(&size),8);
        if(!motion || std::string(magic,8)!="PFCACHE1" || version!=1 || keySize>8192 || size>512ULL*1024*1024)return 6;
        motion.seekg(keySize,std::ios::cur);std::vector<std::uint8_t> data(size);
        motion.read(reinterpret_cast<char*>(data.data()),size);if(!motion)return 6;
        std::vector<pfcore::MotionWindow> windows;int count=0;
        if(!pfservices::deserializeMotionWindows(data,windows,count))return 6;
        for(const auto& w:windows)if(!w.frames.empty()) {
            const auto key=std::pair{w.sceneStartSeconds,w.sceneEndSeconds};
            auto found=lastPoses.find(key);
            if(found==lastPoses.end())lastPoses.emplace(key,w.frames.back().timestampSeconds);
            else found->second=std::max(found->second,w.frames.back().timestampSeconds);
        }
    }
    QJsonArray candidates,endChecks;std::size_t observed=0;
    for(const auto& scene:*scenes) {
        observed+=scene.times.size();
        for(const auto& candidate:pfcore::SceneDetector().sequenceCutCandidates(scene.pixels,scene.times))
            candidates.append(QJsonObject{{"scene",static_cast<qint64>(scene.scene)},
                {"sceneStart",scene.start},{"sceneEnd",scene.end},
                {"before",candidate.beforeSeconds},{"after",candidate.afterSeconds},{"score",candidate.score}});
        const auto poses=lastPoses.find({scene.start,scene.end});
        if(!scene.times.empty() && poses!=lastPoses.end() && poses->second>scene.times.back()+1e-6)
            endChecks.append(QJsonObject{{"scene",static_cast<qint64>(scene.scene)},
                {"sceneStart",scene.start},{"sceneEnd",scene.end},
                {"before",scene.times.back()},{"after",scene.end},{"lastPose",poses->second}});
    }
    QFile output(args[2]);if(!output.open(QIODevice::WriteOnly))return 5;
    output.write(QJsonDocument(QJsonObject{{"confirmedBoundaries",false},{"shots",static_cast<qint64>(scenes->size())},
        {"observations",static_cast<qint64>(observed)},{"candidates",candidates},{"endChecks",endChecks}}).toJson());
    std::cout<<candidates.size()<<" candidate brackets, "<<endChecks.size()<<" end checks from "<<scenes->size()<<" shots\n";
}
