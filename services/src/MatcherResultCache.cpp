#include <pfservices/MatcherResultCache.hpp>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>

namespace pfservices {
namespace {
constexpr qsizetype maximumBytes=16*1024*1024;
constexpr std::size_t maximumMatches=10000;
// One field list for codec symmetry. No object bytes, padding or shared_ptr
// addresses are persisted. Organization evidence must be recomputed.
#define PF_MATCH_DOUBLES(X) \
 X(similarity) X(dtwDistance) X(durationSeconds) \
 X(leftStartSeconds) X(leftEndSeconds) X(rightStartSeconds) X(rightEndSeconds) \
 X(leftSceneStartSeconds) X(leftSceneEndSeconds) X(rightSceneStartSeconds) X(rightSceneEndSeconds) \
 X(rankScore) X(evidenceQuality) X(bestMatchScore) X(redundancyPenalty) X(poseSimilarity) \
 X(leftCanonicalStart) X(leftCanonicalEnd) X(rightCanonicalStart) X(rightCanonicalEnd) \
 X(appearanceSimilarity) X(sceneSimilarity) X(unmirroredSimilarity)
#define PF_MATCH_SIZES(X) X(leftIndex) X(rightIndex) X(continuityRank) X(leftFamilySize) X(rightFamilySize)
#define PF_MATCH_STRINGS(X) X(leftSourceId) X(rightSourceId) X(directionLabel) X(gestureLabel) X(leftSceneFamily) X(rightSceneFamily)
#define PF_MATCH_BOOLS(X) X(appearanceVerified) X(faceVerified) X(headOnlyComparison) X(leftEnabled) X(rightEnabled)
}

std::string matcherResultCacheKey(const std::array<std::uint8_t,32>& fingerprint,
                                 std::string_view dependencies)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArrayView(reinterpret_cast<const char*>(fingerprint.data()),fingerprint.size()));
    hash.addData(QByteArrayView(dependencies.data(),dependencies.size()));
    // Bump when matcher admission/retrieval/selection semantics change.
    return "matcher-results-v1|"+hash.result().toHex().toStdString();
}

std::vector<std::uint8_t> serializeMatcherResults(std::span<const pfcore::MotionMatch> matches)
{
    if(matches.size()>maximumMatches)return {};
    QJsonArray records;
    for(const auto& match:matches) {
        if(match.leftFamilyEvidence || match.rightFamilyEvidence)return {};
        QJsonObject record;
#define WRITE_DOUBLE(name) if(!std::isfinite(match.name))return {};record[#name]=match.name;
        PF_MATCH_DOUBLES(WRITE_DOUBLE)
#undef WRITE_DOUBLE
#define WRITE_SIZE(name) if(match.name>1000000)return {};record[#name]=static_cast<qint64>(match.name);
        PF_MATCH_SIZES(WRITE_SIZE)
#undef WRITE_SIZE
#define WRITE_STRING(name) if(match.name.size()>16384)return {};record[#name]=QString::fromStdString(match.name);
        PF_MATCH_STRINGS(WRITE_STRING)
#undef WRITE_STRING
#define WRITE_BOOL(name) record[#name]=match.name;
        PF_MATCH_BOOLS(WRITE_BOOL)
#undef WRITE_BOOL
        record["rejection"]=static_cast<int>(match.rejection);
        records.append(record);
    }
    auto bytes=QJsonDocument(QJsonObject{{"schema",1},{"matches",records}}).toJson(QJsonDocument::Compact);
    if(bytes.size()>maximumBytes-40)return {};
    bytes.prepend("PFMATCH1",8);
    bytes.append(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256));
    return {bytes.begin(),bytes.end()};
}

bool deserializeMatcherResults(std::span<const std::uint8_t> input,
    const std::vector<pfcore::MotionWindow>& windows,std::vector<pfcore::MotionMatch>& matches)
{
    if(input.size()<40 || input.size()>maximumBytes
        || !std::equal(input.begin(),input.begin()+8,"PFMATCH1"))return false;
    const auto data=QByteArrayView(reinterpret_cast<const char*>(input.data()),input.size()-32);
    const auto digest=QCryptographicHash::hash(data,QCryptographicHash::Sha256);
    if(!std::equal(input.end()-32,input.end(),reinterpret_cast<const std::uint8_t*>(digest.data())))return false;
    QJsonParseError error;
    const auto document=QJsonDocument::fromJson(QByteArray(data.data()+8,data.size()-8),&error);
    if(error.error!=QJsonParseError::NoError || !document.isObject())return false;
    const auto root=document.object();
    if(root["schema"].toInt()!=1 || !root["matches"].isArray()
        || static_cast<std::size_t>(root["matches"].toArray().size())>maximumMatches)return false;
    std::vector<pfcore::MotionMatch> parsed;
    for(const auto& value:root["matches"].toArray()) {
        if(!value.isObject())return false;
        const auto record=value.toObject();pfcore::MotionMatch match;
#define READ_DOUBLE(name) if(!record[#name].isDouble() || !std::isfinite(record[#name].toDouble()))return false;match.name=record[#name].toDouble();
        PF_MATCH_DOUBLES(READ_DOUBLE)
#undef READ_DOUBLE
#define READ_SIZE(name) {const auto number=record[#name].toDouble(-1);if(!std::isfinite(number) || number<0 || number>1000000 || std::floor(number)!=number)return false;match.name=static_cast<std::size_t>(number);}
        PF_MATCH_SIZES(READ_SIZE)
#undef READ_SIZE
#define READ_STRING(name) if(!record[#name].isString() || record[#name].toString().size()>16384)return false;match.name=record[#name].toString().toStdString();
        PF_MATCH_STRINGS(READ_STRING)
#undef READ_STRING
#define READ_BOOL(name) if(!record[#name].isBool())return false;match.name=record[#name].toBool();
        PF_MATCH_BOOLS(READ_BOOL)
#undef READ_BOOL
        const auto rejection=record["rejection"].toInt(-1);
        if(rejection<0 || rejection>static_cast<int>(pfcore::MotionRejection::Score))return false;
        match.rejection=static_cast<pfcore::MotionRejection>(rejection);
        if(match.leftIndex>=windows.size() || match.rightIndex>=windows.size() || match.leftIndex==match.rightIndex
            || match.similarity<0 || match.similarity>1 || match.durationSeconds<0)return false;
        const auto validSide=[](const auto& window,const std::string& source,double start,double end) {
            return source==window.sourceId && !window.frames.empty() && end>start
                && start>=window.frames.front().timestampSeconds-1e-6
                && end<=window.frames.back().timestampSeconds+1e-6;
        };
        if(!validSide(windows[match.leftIndex],match.leftSourceId,match.leftStartSeconds,match.leftEndSeconds)
            || !validSide(windows[match.rightIndex],match.rightSourceId,match.rightStartSeconds,match.rightEndSeconds))return false;
        parsed.push_back(std::move(match));
    }
    matches=std::move(parsed);return true;
}
#undef PF_MATCH_DOUBLES
#undef PF_MATCH_SIZES
#undef PF_MATCH_STRINGS
#undef PF_MATCH_BOOLS
}
