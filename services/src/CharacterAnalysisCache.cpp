#include <pfservices/CharacterAnalysisCache.hpp>
#include <pfservices/MotionWindowCache.hpp>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
#include <cmath>
#include <set>
#include <limits>
#include <algorithm>

namespace pfservices {
namespace {
QJsonArray vectorJson(const std::vector<float>& values)
{
    QJsonArray result; for (float v : values) result.append(v); return result;
}
bool vectorFromJson(const QJsonValue& json, std::vector<float>& values)
{
    if (!json.isArray() || json.toArray().size() > 4096) return false;
    for (const auto& item : json.toArray()) {
        if (!item.isDouble() || !std::isfinite(item.toDouble())
            || std::abs(item.toDouble()) > std::numeric_limits<float>::max()) return false;
        values.push_back(static_cast<float>(item.toDouble()));
    }
    return true;
}
bool idFromJson(const QJsonValue& value, std::size_t& result)
{
    bool ok = false; const auto id = value.toString().toULongLong(&ok);
    if (!ok || id == 0 || id > std::numeric_limits<std::size_t>::max()) return false;
    result = static_cast<std::size_t>(id); return true;
}
bool unit(double value) { return std::isfinite(value) && value >= 0 && value <= 1; }
}

std::vector<std::uint8_t> serializeCharacterAnalysis(const CharacterAnalysis& analysis,
    const std::function<bool()>& cancelled)
{
    QJsonArray candidates;
    for (const auto& c : analysis.candidates) {
        if (cancelled && cancelled()) return {};
        QJsonArray tracks, appearances;
        for (auto id : c.trackIds) tracks.append(QString::number(id));
        for (const auto& a : c.appearances) appearances.append(QJsonObject{
            {"track",QString::number(a.trackId)},{"time",a.timestampSeconds},{"quality",a.quality},
            {"box",QJsonArray{a.box.left,a.box.top,a.box.right,a.box.bottom}}});
        candidates.append(QJsonObject{{"id",QString::number(c.id)},{"tracks",tracks},
            {"body",vectorJson(c.prototype.body)},{"face",vectorJson(c.prototype.face)},
            {"bodyEvidence",c.prototype.bodyEvidence},{"faceEvidence",c.prototype.faceEvidence},
            {"observations",QString::number(c.observationCount)},{"presence",c.presenceSeconds},
            {"quality",c.evidenceQuality},{"appearances",appearances}});
    }
    const auto header = QJsonDocument(QJsonObject{{"schema",1},{"candidates",candidates}}).toJson(QJsonDocument::Compact);
    std::vector<std::uint8_t> result {'P','F','C','H',2};
    const auto size = static_cast<std::uint32_t>(header.size());
    for (int i=0;i<4;++i) result.push_back(static_cast<std::uint8_t>(size >> (8*i)));
    result.insert(result.end(),header.begin(),header.end());
    const auto windows = serializeMotionWindows(analysis.windows,analysis.sceneCount,cancelled);
    if (windows.empty() || (cancelled && cancelled())) return {};
    result.insert(result.end(),windows.begin(),windows.end());
    const auto digest=QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char*>(result.data()),result.size()),QCryptographicHash::Sha256);
    result.insert(result.end(),digest.begin(),digest.end());
    return result;
}

bool deserializeCharacterAnalysis(std::span<const std::uint8_t> bytes, CharacterAnalysis& analysis)
{
    if (bytes.size()<41 || bytes[0]!='P' || bytes[1]!='F' || bytes[2]!='C' || bytes[3]!='H' || bytes[4]!=2) return false;
    const auto payload=bytes.first(bytes.size()-32);
    const auto digest=QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char*>(payload.data()),payload.size()),QCryptographicHash::Sha256);
    if (!std::equal(bytes.end()-32,bytes.end(),reinterpret_cast<const std::uint8_t*>(digest.constData()))) return false;
    bytes=payload;
    std::uint32_t size=0; for (int i=0;i<4;++i) size |= std::uint32_t(bytes[5+i]) << (8*i);
    if (size > 16*1024*1024 || size > bytes.size()-9) return false;
    QJsonParseError error;
    const auto document=QJsonDocument::fromJson(QByteArray(reinterpret_cast<const char*>(bytes.data()+9),size),&error);
    if (error.error!=QJsonParseError::NoError || !document.isObject()) return false;
    const auto root=document.object();
    if (root.value("schema").toInt()!=1 || !root.value("candidates").isArray()
        || root.value("candidates").toArray().size()>100000) return false;
    CharacterAnalysis parsed;
    std::set<std::size_t> ids, tracks;
    for (const auto& value : root.value("candidates").toArray()) {
        if (!value.isObject()) return false;
        const auto object=value.toObject(); pfcore::CharacterCandidate c;
        if (!idFromJson(object.value("id"),c.id) || !ids.insert(c.id).second
            || !idFromJson(object.value("observations"),c.observationCount)
            || !object.value("tracks").isArray() || !object.value("appearances").isArray()
            || !vectorFromJson(object.value("body"),c.prototype.body)
            || !vectorFromJson(object.value("face"),c.prototype.face)) return false;
        for (const auto& track : object.value("tracks").toArray()) {
            std::size_t id=0;
            if (!idFromJson(track,id) || !tracks.insert(id).second) return false;
            c.trackIds.push_back(id);
        }
        if (c.trackIds.empty()) return false;
        const auto number = [&](const char* key) {return object.value(key).toDouble(-1);};
        c.presenceSeconds=number("presence"); c.evidenceQuality=number("quality");
        c.prototype.bodyEvidence=number("bodyEvidence"); c.prototype.faceEvidence=number("faceEvidence");
        c.prototype.duration=c.presenceSeconds;
        if (!std::isfinite(c.presenceSeconds) || c.presenceSeconds<0 || !unit(c.evidenceQuality)
            || !unit(c.prototype.bodyEvidence) || !unit(c.prototype.faceEvidence)) return false;
        if (object.value("appearances").toArray().size()>5) return false;
        for (const auto& appearance : object.value("appearances").toArray()) {
            const auto item=appearance.toObject(); pfcore::CharacterAppearance a;
            if (!idFromJson(item.value("track"),a.trackId)
                || std::find(c.trackIds.begin(),c.trackIds.end(),a.trackId)==c.trackIds.end()) return false;
            a.timestampSeconds=item.value("time").toDouble(-1); a.quality=item.value("quality").toDouble(-1);
            if (!std::isfinite(a.timestampSeconds) || a.timestampSeconds<0 || !unit(a.quality)) return false;
            const auto box=item.value("box").toArray();
            if (box.size()!=4) return false;
            for (const auto& v : box) if (!v.isDouble() || !unit(v.toDouble())) return false;
            a.box={box[0].toDouble(),box[1].toDouble(),box[2].toDouble(),box[3].toDouble()};
            if (a.box.right<a.box.left || a.box.bottom<a.box.top) return false;
            c.appearances.push_back(a);
        }
        parsed.candidates.push_back(std::move(c));
    }
    if (!deserializeMotionWindows(bytes.subspan(9+size),parsed.windows,parsed.sceneCount)) return false;
    for (const auto& w : parsed.windows) if (!tracks.contains(w.trackId)) return false;
    analysis=std::move(parsed);
    return true;
}
}
