#include <pfcore/DominantPerson.hpp>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <iostream>
#include <map>
#include <chrono>

// Replay frozen observations through production tracking/clustering. No source
// decoding, model inference, settings writes, or biometric output.
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);const auto args=app.arguments();
    if(args.size()<3||args.size()>4){std::cerr<<"usage: pf_character_probe tracks.json scene-log [minimumBodySimilarity]\n";return 2;}
    QFile file(args[1]);if(!file.open(QIODevice::ReadOnly))return 2;
    const auto document=QJsonDocument::fromJson(file.readAll());if(!document.isObject())return 2;
    std::map<double,std::vector<pfcore::PersonDetection>> frames;
    for(const auto& value:document.object()["tracks"].toArray())for(const auto& item:value.toObject()["observations"].toArray()) {
        const auto o=item.toObject();pfcore::PersonDetection p;
        p.timestampSeconds=o["time"].toDouble();p.frameDurationSeconds=o["duration"].toDouble();
        p.confidence=o["confidence"].toDouble();p.keypointConfidence=o["keypointConfidence"].toDouble();
        p.appearanceConfidence=o["appearanceConfidence"].toDouble();
        const auto b=o["box"].toArray();if(b.size()!=4)return 2;
        p.box={b[0].toDouble(),b[1].toDouble(),b[2].toDouble(),b[3].toDouble()};
        for(const auto& v:o["points"].toArray()){const auto q=v.toArray();if(q.size()!=3)return 2;p.keypoints.push_back({q[0].toDouble(),q[1].toDouble(),q[2].toDouble()});}
        for(const auto& v:o["face"].toArray())p.faceEmbedding.push_back(v.toDouble());
        for(const auto& v:o["body"].toArray())p.appearanceEmbedding.push_back(v.toDouble());
        frames[p.timestampSeconds].push_back(std::move(p));
    }
    QFile log(args[2]);if(!log.open(QIODevice::ReadOnly))return 2;
    const auto text=QString::fromUtf8(log.readAll());QRegularExpression pattern("PF_DEBUG_SCENE cut=([0-9.]+)");
    auto it=pattern.globalMatch(text);std::vector<double> cuts;while(it.hasNext())cuts.push_back(it.next().captured(1).toDouble());
    const auto start=std::chrono::steady_clock::now();
    pfcore::PersonTracker tracker(.30,1.0,true,args.size()==4?args[3].toDouble():.65);
    for(const auto& [time,people]:frames)tracker.update(time,0,people);
    tracker.retrackScenes(cuts);
    const auto characters=pfcore::characterCandidates(tracker.tracks());QJsonArray output,assignments;
    std::map<std::size_t,std::size_t> identities;
    for(const auto& c:characters) {
        QJsonArray tracks,appearances;
        for(auto id:c.trackIds){tracks.append(QString::number(id));identities[id]=c.id;}
        for(const auto& a:c.appearances)appearances.append(QJsonObject{{"time",a.timestampSeconds},{"track",QString::number(a.trackId)}});
        output.append(QJsonObject{{"id",QString::number(c.id)},{"tracks",tracks},{"presence",c.presenceSeconds},
            {"distinctIdentity",c.distinctIdentity},
            {"faceEvidence",c.prototype.faceEvidence},{"bodyEvidence",c.prototype.bodyEvidence},{"appearances",appearances}});
    }
    for(const auto& track:tracker.tracks())for(const auto& o:track.observations) {
        const auto identity=identities.find(track.id);if(identity==identities.end())continue;
        assignments.append(QJsonObject{{"time",o.timestampSeconds},{"track",QString::number(track.id)},{"identity",QString::number(identity->second)},
            {"box",QJsonArray{o.box.left,o.box.top,o.box.right,o.box.bottom}}});
    }
    std::cout<<QJsonDocument(QJsonObject{{"candidates",output},{"trackCount",static_cast<qint64>(tracker.tracks().size())},
        {"assignments",assignments},
        {"elapsedMs",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()}}).toJson().constData();
}
