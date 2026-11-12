#include <pfservices/HeadOrientationCache.hpp>
#include <pfservices/PfCache.hpp>
#include <pfcore/PathEncoding.hpp>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
QString fileHash(const QString& path) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot read fingerprint input");
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if(!hash.addData(&file))throw std::runtime_error("Cannot fingerprint input");
    return QString::fromLatin1(hash.result().toHex());
}
QJsonObject readJson(const QString& path) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("Cannot read JSON input");
    QJsonParseError error;const auto document=QJsonDocument::fromJson(file.readAll(),&error);
    if(error.error!=QJsonParseError::NoError || !document.isObject())throw std::runtime_error("Invalid JSON input");
    return document.object();
}
std::string sourceFingerprint(const std::string& source) {
    const auto path=pfcore::pathFromUtf8(source);
    // Same existing namespace contract as AnalysisController. The separate
    // full source hash below verifies this replay's input content, not its key.
    return pfcore::pathToUtf8(path)+"|"+std::to_string(std::filesystem::file_size(path))+"|"
        +std::to_string(std::filesystem::last_write_time(path).time_since_epoch().count());
}
std::optional<std::vector<std::uint8_t>> readRecord(const QString& path,const std::string& key) {
    if(QFileInfo(path).isSymLink())return {};
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};
    const auto header=file.read(24);if(header.size()!=24 || header.left(8)!="PFCACHE1")return {};
    std::uint32_t version=0,length=0;std::uint64_t size=0;
    std::memcpy(&version,header.constData()+8,4);std::memcpy(&length,header.constData()+12,4);
    std::memcpy(&size,header.constData()+16,8);
    if(version!=1 || !length || length>16384 || size>1024*1024 || file.size()!=24+length+static_cast<qint64>(size))return {};
    if(file.read(length)!=QByteArray::fromStdString(key))return {};
    const auto bytes=file.read(static_cast<qint64>(size));
    if(bytes.size()!=static_cast<qint64>(size))return {};
    return std::vector<std::uint8_t>(bytes.begin(),bytes.end());
}
}

// Reads exact request-key cache records. No get()/LRU touch, decoder, inference,
// matcher, cache writer or eviction. Missing/invalid data is reported, not filled.
int main(int argc,char** argv) {
    if(argc!=4){std::cerr<<"Usage: pf_head_cache_replay REQUESTS.json CONFIG.json NEW-OUTPUT.json\n";return 2;}
    try {
        const auto requests=readJson(QString::fromUtf8(argv[1]));
        const auto config=readJson(QString::fromUtf8(argv[2]));
        if(requests["schemaVersion"].toInt()!=1 || config["schemaVersion"].toInt()!=1)return 3;
        const auto cacheRoot=QFileInfo(config["cacheRoot"].toString()).canonicalFilePath();
        const auto primaryRoot=QFileInfo(config["primaryCacheRoot"].toString()).canonicalFilePath();
        if(cacheRoot.isEmpty() || primaryRoot.isEmpty() || !QFileInfo(cacheRoot).isDir() || !QFileInfo(primaryRoot).isDir())return 3;
        const QFileInfo outputInfo(QString::fromUtf8(argv[3]));
        const auto parent=outputInfo.dir().canonicalPath();
        const auto outputPath=parent+'/'+outputInfo.fileName();
        if(parent.isEmpty() || outputInfo.exists() || outputPath.startsWith(cacheRoot+'/',Qt::CaseInsensitive)
            || outputPath.startsWith(primaryRoot+'/',Qt::CaseInsensitive))return 3;
        const auto provider=config["providerCode"].toInt(-1);
        if(provider<0 || provider>4 || config["providerCode"].toDouble()!=provider)return 3;
        const auto modelHash=fileHash(config["headModel"].toString());
        const auto detectorHash=fileHash(config["faceDetector"].toString());
        const auto modelIdentity=(modelHash+"|detector="+detectorHash).toStdString();
        pfservices::PfCache primary(pfcore::pathFromUtf8(primaryRoot.toStdString()));
        std::map<std::string,std::string> namespaces;
        QJsonObject verifiedSources;
        const auto expectedSources=config["sourceSha256"].toObject();
        for(auto it=expectedSources.begin();it!=expectedSources.end();++it) {
            const auto actual=fileHash(it.key());
            if(!it.value().isString() || actual!=it.value().toString())return 4;
            const auto source=it.key().toStdString();
            namespaces.emplace(source,sourceFingerprint(source)+"|generation="+primary.sourceGeneration(source));
            verifiedSources.insert(it.key(),actual);
        }
        const auto pairs=requests["pairs"].toArray();if(pairs.empty() || pairs.size()>10000)return 3;
        QJsonArray outputPairs;std::size_t requested=0,hits=0,observed=0,missing=0;
        for(const auto& pairValue:pairs) {
            if(!pairValue.isObject())return 3;
            const auto pair=pairValue.toObject();QJsonObject out{{"returnedArrayIndex",pair["returnedArrayIndex"]}};
            for(const auto& side:{QStringLiteral("left"),QStringLiteral("right")}) {
                const auto input=pair[side].toObject();const auto source=input["source"].toString().toStdString();
                if(!namespaces.contains(source))return 4;
                const double start=input["start"].toDouble(-1),end=input["end"].toDouble(-1);
                if(!std::isfinite(start) || !std::isfinite(end) || start<0 || end<=start)return 3;
                const auto frames=input["frames"].toArray();if(frames.empty() || frames.size()>10000)return 3;
                QJsonArray samples;double previous=-1;
                for(const auto& frameValue:frames) {
                    const auto frameJson=frameValue.toObject();
                    pfcore::PoseFrame frame;frame.timestampSeconds=frameJson["time"].toDouble(-1);
                    if(!std::isfinite(frame.timestampSeconds) || frame.timestampSeconds<=previous
                        || frame.timestampSeconds<start-1e-6 || frame.timestampSeconds>end+1e-6)return 3;
                    previous=frame.timestampSeconds;
                    const auto points=frameJson["points"].toArray();if(points.size()!=17)return 3;
                    for(const auto& pointValue:points) {
                        const auto point=pointValue.toArray();if(point.size()!=3)return 3;
                        for(const auto& value:point)if(!value.isDouble() || !std::isfinite(value.toDouble()))return 3;
                        frame.keypoints.push_back({point[0].toDouble(),point[1].toDouble(),point[2].toDouble()});
                    }
                    const auto key=pfservices::headOrientationCacheKey(source,frame,namespaces.at(source),modelIdentity,provider);
                    const auto keyHash=QCryptographicHash::hash(QByteArray::fromStdString(key),QCryptographicHash::Sha256).toHex();
                    const auto bytes=readRecord(cacheRoot+'/'+QString::fromLatin1(keyHash)+".pfc",key);
                    const auto sample=bytes?pfservices::deserializeHeadOrientationSample(*bytes,frame.timestampSeconds):std::nullopt;
                    ++requested;QJsonObject record{{"time",frame.timestampSeconds},{"keySha256",QString::fromLatin1(keyHash)},
                        {"cacheStatus",sample?"hit":"missing_or_invalid"},{"observed",sample&&sample->observed}};
                    QJsonArray rotation;
                    if(sample) {
                        ++hits;if(sample->observed){++observed;for(const auto value:sample->rotation)rotation.append(value);}
                        record["payloadSha256"]=QString::fromLatin1(QCryptographicHash::hash(QByteArrayView(
                            reinterpret_cast<const char*>(bytes->data()),bytes->size()),QCryptographicHash::Sha256).toHex());
                    }else ++missing;
                    record["rotation"]=rotation;samples.append(record);
                }
                out[side]=QJsonObject{{"source",QString::fromStdString(source)},{"trackId",input["trackId"]},
                    {"start",start},{"end",end},{"samples",samples}};
            }
            outputPairs.append(out);
        }
        const QJsonObject output{{"schemaVersion",1},{"scope","Exact-key read-only head orientation replay; no decoder/inference/matcher or relevance fitting"},
            {"requestSha256",fileHash(QString::fromUtf8(argv[1]))},{"configSha256",fileHash(QString::fromUtf8(argv[2]))},
            {"headModelSha256",modelHash},{"faceDetectorSha256",detectorHash},{"providerCode",provider},
            {"verifiedSourceSha256",verifiedSources},{"requested",static_cast<qint64>(requested)},
            {"cached",static_cast<qint64>(hits)},{"observed",static_cast<qint64>(observed)},
            {"missingOrInvalid",static_cast<qint64>(missing)},{"pairs",outputPairs}};
        QFile file(outputPath);if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly))return 5;
        const auto encoded=QJsonDocument(output).toJson();if(file.write(encoded)!=encoded.size())return 5;
        return missing?6:0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 7;}
}
