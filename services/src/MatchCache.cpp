#include <pfservices/MatchCache.hpp>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include <algorithm>
#include <limits>
#include <type_traits>

namespace pfservices {
namespace {
constexpr std::size_t kLimit = 8U * 1024U * 1024U;
constexpr std::size_t kMaxMatches = 50000;

// Explicit fields avoid padding bytes and keep exact floating-point inputs.
// Add new matcher settings here when extending MotionMatcherParams.
template<class Visitor> void parameters(const pfcore::MotionMatcherParams& p, Visitor visit)
{
    // Aggregate binding deliberately fails to compile if a new parameter
    // is added without updating this complete cache-key visitor.
    const auto& [similarityThreshold, candidateThreshold, minRepeatGapSec, sameFileGapSec, crossFileGapSec, duplicateWindowSec, noiseFactor, maxUniqueResults, maxResultsPerShot, maxComparisonThreads, expandedSearch, timeWeight, normalizeSize, mirrorInvariant, dtwBand, sakoeChibaRatio, motionDeltaThreshold, minActiveTransitionRatio, minMotionRange, minMotionSpanSec, temporalSimilarityThreshold, staticPoseSimilarityThreshold, staticArticulationSimilarityThreshold, minTemporalFrames, minTemporalDurationSec, sameSourceGapFloorSec, nmsOverlapThreshold, requireSameTrackWithinSource, allowStaticFrames, requireAppearance, minAppearanceSimilarity, appearanceWeight, minAppearanceEvidence, minFaceSimilarity, minHeadFaceSimilarity, sameSceneContextThreshold, sameSceneContextGapSec] = p;
    visit(similarityThreshold);
    visit(candidateThreshold);
    visit(minRepeatGapSec);
    visit(sameFileGapSec);
    visit(crossFileGapSec);
    visit(duplicateWindowSec);
    visit(noiseFactor);
    visit(maxUniqueResults);
    visit(maxResultsPerShot);
    visit(maxComparisonThreads);
    visit(expandedSearch);
    visit(timeWeight);
    visit(normalizeSize);
    visit(mirrorInvariant);
    visit(dtwBand);
    visit(sakoeChibaRatio);
    visit(motionDeltaThreshold);
    visit(minActiveTransitionRatio);
    visit(minMotionRange);
    visit(minMotionSpanSec);
    visit(temporalSimilarityThreshold);
    visit(staticPoseSimilarityThreshold);
    visit(staticArticulationSimilarityThreshold);
    visit(minTemporalFrames);
    visit(minTemporalDurationSec);
    visit(sameSourceGapFloorSec);
    visit(nmsOverlapThreshold);
    visit(requireSameTrackWithinSource);
    visit(allowStaticFrames);
    visit(requireAppearance);
    visit(minAppearanceSimilarity);
    visit(appearanceWeight);
    visit(minAppearanceEvidence);
    visit(minFaceSimilarity);
    visit(minHeadFaceSimilarity);
    visit(sameSceneContextThreshold);
    visit(sameSceneContextGapSec);
}

template<class Match, class Visitor> void matchFields(Match& m, Visitor visit)
{
#define FIELD(name) visit(#name, m.name)
    FIELD(leftIndex); FIELD(rightIndex); FIELD(leftSourceId); FIELD(rightSourceId);
    FIELD(similarity); FIELD(dtwDistance); FIELD(durationSeconds);
    FIELD(leftStartSeconds); FIELD(leftEndSeconds); FIELD(rightStartSeconds); FIELD(rightEndSeconds);
    FIELD(leftSceneStartSeconds); FIELD(leftSceneEndSeconds); FIELD(rightSceneStartSeconds); FIELD(rightSceneEndSeconds);
    FIELD(directionLabel); FIELD(gestureLabel); FIELD(rankScore);
    FIELD(appearanceSimilarity); FIELD(sceneSimilarity); FIELD(appearanceVerified); FIELD(faceVerified);
    FIELD(headOnlyComparison); FIELD(unmirroredSimilarity);
#undef FIELD
}

class Fingerprint {
public:
    template<class T> void value(const T& value) {
        if constexpr (std::is_same_v<T, bool>) scalar(static_cast<std::uint8_t>(value));
        else if constexpr (std::is_integral_v<T>) scalar(static_cast<std::uint64_t>(value));
        else scalar(value);
    }
    void text(const std::string& value) {
        this->value(value.size());
        hash_.addData(QByteArrayView(value.data(), static_cast<qsizetype>(value.size())));
    }
    template<class T> void array(const std::vector<T>& values) {
        value(values.size());
        // float embeddings and the three-double keypoint layout have no
        // padding; process the existing buffers, without a second frame copy.
        static_assert(std::is_same_v<T,float> || (std::is_same_v<T,pfcore::Keypoint> && sizeof(T)==3*sizeof(double)));
        if (!values.empty()) hash_.addData(QByteArrayView(reinterpret_cast<const char*>(values.data()),
            static_cast<qsizetype>(values.size()*sizeof(T))));
    }
    std::string finish() { return hash_.result().toHex().toStdString(); }
private:
    template<class T> void scalar(const T& value) {
        hash_.addData(QByteArrayView(reinterpret_cast<const char*>(&value), sizeof(value)));
    }
    QCryptographicHash hash_{QCryptographicHash::Sha256};
};
}

std::string MatchCache::key(std::span<const pfcore::MotionWindow> windows,
                            const pfcore::MotionMatcherParams& params)
{
    Fingerprint hash;
    hash.text("ranked-matches-v1");
    hash.text(PF_MATCHER_CONTRACT);
    parameters(params, [&](const auto& value) { hash.value(value); });
    hash.value(windows.size());
    for (const auto& w : windows) {
        hash.text(w.sourceId); hash.value(w.trackId); hash.value(w.sceneIndex);
        hash.value(w.hasSceneIndex); hash.value(w.staticFrameSet);
        hash.value(w.sceneStartSeconds); hash.value(w.sceneEndSeconds);
        hash.value(w.appearanceConfidence); hash.array(w.appearanceEmbedding);
        hash.value(w.faceConfidence); hash.array(w.faceEmbedding); hash.array(w.sceneContext);
        hash.value(w.frames.size());
        for (const auto& frame : w.frames) { hash.value(frame.timestampSeconds); hash.array(frame.keypoints); }
    }
    return "ranked-matches-v1|"+hash.finish();
}

bool MatchCache::store(PfCache& cache, const std::string& key,
                        std::span<const pfcore::MotionMatch> matches, std::string& error)
{
    if (matches.size()>kMaxMatches) return false;
    QJsonArray records;
    std::size_t budget = 0;
    for (const auto& match : matches) {
        QJsonObject record;
        bool valid = true;
        matchFields(match, [&](const char* name, const auto& value) {
            using T = std::remove_cvref_t<decltype(value)>;
            const auto field = QString::fromLatin1(name);
            budget += 128; // field names, separators and numeric text
            if constexpr (std::is_same_v<T,std::string>) {
                if (value.size()>kLimit/6) { valid = false; return; }
                budget += value.size()*6;
                if (budget<=kLimit) record[field] = QString::fromStdString(value);
            } else if constexpr (std::is_same_v<T,bool>) record[field] = value;
            else if constexpr (std::is_integral_v<T>) record[field] = QString::number(static_cast<qulonglong>(value));
            else { valid = valid && std::isfinite(value); record[field] = value; }
        });
        if (!valid || budget>kLimit) return false;
        records.append(record);
    }
    const auto bytes = QJsonDocument(QJsonObject{{"version",1},{"matches",records}}).toJson(QJsonDocument::Compact);
    if (static_cast<std::size_t>(bytes.size())+32>kLimit) return false;
    const auto digest = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    std::vector<std::uint8_t> payload;
    payload.reserve(static_cast<std::size_t>(bytes.size())+32);
    payload.insert(payload.end(), digest.begin(), digest.end());
    payload.insert(payload.end(), bytes.begin(), bytes.end());
    return cache.put(key, payload, error);
}

std::optional<std::vector<pfcore::MotionMatch>> MatchCache::load(
    const PfCache& cache, const std::string& key, std::span<const pfcore::MotionWindow> windows)
{
    const auto payload = cache.get(key, kLimit);
    if (!payload || payload->size()<32) return std::nullopt;
    const auto bytes = QByteArrayView(reinterpret_cast<const char*>(payload->data()+32),
                                      static_cast<qsizetype>(payload->size()-32));
    const auto digest = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    if (!std::equal(digest.begin(),digest.end(),reinterpret_cast<const char*>(payload->data()))) return std::nullopt;
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(QByteArray(bytes.data(),bytes.size()), &parse);
    if (parse.error!=QJsonParseError::NoError || !document.isObject()) return std::nullopt;
    const auto object = document.object();
    if (object.value("version").toInt()!=1 || !object.value("matches").isArray()) return std::nullopt;
    const auto records = object.value("matches").toArray();
    if (records.size()>static_cast<qsizetype>(kMaxMatches)) return std::nullopt;
    std::vector<pfcore::MotionMatch> matches;
    matches.reserve(static_cast<std::size_t>(records.size()));
    for (const auto& item : records) {
        if (!item.isObject()) return std::nullopt;
        const auto record = item.toObject();
        pfcore::MotionMatch match;
        bool valid = true;
        matchFields(match, [&](const char* name, auto& value) {
            using T = std::remove_cvref_t<decltype(value)>;
            const auto field = record.value(QString::fromLatin1(name));
            if constexpr (std::is_same_v<T,std::string>) { valid = valid && field.isString(); value = field.toString().toStdString(); }
            else if constexpr (std::is_same_v<T,bool>) { valid = valid && field.isBool(); value = field.toBool(); }
            else if constexpr (std::is_integral_v<T>) {
                bool ok = false;
                const auto index = field.toString().toULongLong(&ok);
                valid = valid && field.isString() && ok && index<=std::numeric_limits<T>::max();
                value = static_cast<T>(index);
            } else { valid = valid && field.isDouble(); value = field.toDouble(); valid = valid && std::isfinite(value); }
        });
        if (!valid || match.leftIndex>=windows.size() || match.rightIndex>=windows.size()
            || match.leftIndex==match.rightIndex || match.similarity<0 || match.similarity>1
            || match.leftSourceId!=windows[match.leftIndex].sourceId || match.rightSourceId!=windows[match.rightIndex].sourceId)
            return std::nullopt;
        const auto inWindow = [](double start, double end, const pfcore::MotionWindow& window) {
            return !window.frames.empty() && start>=0 && end>=start
                && start>=window.frames.front().timestampSeconds-1e-6
                && end<=window.frames.back().timestampSeconds+1e-6;
        };
        if (match.durationSeconds<0 || match.dtwDistance<0
            || !inWindow(match.leftStartSeconds, match.leftEndSeconds, windows[match.leftIndex])
            || !inWindow(match.rightStartSeconds, match.rightEndSeconds, windows[match.rightIndex])) return std::nullopt;
        matches.push_back(std::move(match));
    }
    return matches;
}
} // namespace pfservices
