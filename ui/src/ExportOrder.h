#pragma once

#include <pfcore/MotionMatcher.hpp>
#include <QStringList>
#include <QCryptographicHash>
#include <QVariantList>
#include <algorithm>
#include <tuple>
#include <vector>

namespace pfui {
inline QString exportGroupKey(const QVariantMap& record) {
    const auto group = record.value("manualGroup").toString();
    return group.isEmpty() ? record.value("classification", "unknown").toString()
                           : QStringLiteral("group:") + group;
}
// Preserve an explicit dialog order, including interleaved custom/ML groups.
// Other callers can pass their ML-only order: missing custom groups are then
// inserted before the first ML class, so MCP and GUI exports agree.
inline QStringList effectiveExportClassOrder(const QVariantList& records,
    const QStringList& requestedClassOrder)
{
    QStringList result;
    for (const auto& key : requestedClassOrder)
        if (!key.isEmpty() && !result.contains(key)) result.append(key);
    QStringList missing;
    for (const auto& value : records) {
        const auto record = value.toMap();
        if (record.value("hidden").toBool()) continue;
        if (record.value("manualGroup").toString().isEmpty()) continue;
        const auto key = exportGroupKey(record);
        if (!result.contains(key) && !missing.contains(key)) missing.append(key);
    }
    std::sort(missing.begin(), missing.end());
    auto insertion = std::find_if(result.begin(), result.end(), [](const QString& key) {
        return !key.startsWith("group:");
    }) - result.begin();
    for (const auto& key : missing) result.insert(insertion++, key);
    return result;
}
inline QString exportGroupFileName(const QString& group)
{
    QString slug;
    const auto codePoints = group.toUcs4();
    int count = 0;
    for (const auto point : codePoints) {
        if (count++ >= 48) break;
        const auto category = QChar::category(point);
        const bool forbidden = point < 32 || point == 127
            || category == QChar::Other_Control || category == QChar::Other_Surrogate
            || (point < 128 && QStringLiteral("<>:\"/\\|?*").contains(QChar(static_cast<ushort>(point))));
        const char32_t safePoint = forbidden ? U'_' : point;
        slug += QString::fromUcs4(&safePoint, 1);
    }
    slug = slug.trimmed();
    while (!slug.isEmpty() && (slug.endsWith('.') || slug.back().isSpace())) slug.chop(1);
    if (slug.isEmpty()) slug = QStringLiteral("unnamed");
    const auto digest = QCryptographicHash::hash(group.toUtf8(), QCryptographicHash::Sha256).toHex().left(8);
    // A fixed prefix also avoids reserved Windows device names such as CON.
    return QStringLiteral("group_") + slug + '-' + QString::fromLatin1(digest);
}
inline QString exportClassKey(const QString& value,const QStringList& classOrder) {
    if (value.startsWith("group:")) return value;
    const auto key=value.section('/',0,0);
    if(classOrder.isEmpty() || classOrder.contains(key))return key;
    if(key.contains('+')) {
        auto actions=key.split('+');actions.sort();
        if(!actions.contains(QString()) && std::adjacent_find(actions.begin(),actions.end())==actions.end()) {
            for(const auto& candidate:classOrder) {
                auto expected=candidate.split('+');expected.sort();
                if(expected==actions)return candidate;
            }
        }
    }
    return QStringLiteral("unknown");
}
// Shared class ordering for clip numbering and metadata exports. When a
// class order is supplied, the montage also preserves this input sequence.
inline std::vector<int> orderedExportIndexes(const QVariantList& indexes,
    const std::vector<pfcore::MotionMatch>& matches, const QVariantList& records,
    int numberingMode, const QStringList& classOrder)
{
    std::vector<int> ordered;
    std::vector<bool> seen(matches.size(), false);
    for (const auto& value : indexes) {
        bool ok = false;
        const int index = value.toInt(&ok);
        if (!ok || index < 0 || index >= static_cast<int>(matches.size()) || seen[index]) continue;
        seen[index] = true;
        ordered.push_back(index);
    }
    if (numberingMode == 0) std::stable_sort(ordered.begin(), ordered.end(), [&](int left, int right) {
        const auto& a = matches[left];
        const auto& b = matches[right];
        const auto key = [](const auto& match) {
            return std::tuple{match.leftEnabled ? match.leftSourceId : match.rightSourceId,
                match.leftEnabled ? match.leftStartSeconds : match.rightStartSeconds,
                match.rightEnabled ? match.rightSourceId : std::string{},
                match.rightEnabled ? match.rightStartSeconds : 0.0};
        };
        return key(a) < key(b);
    });
    if (!classOrder.isEmpty()) {
        std::vector<qsizetype> ranks(matches.size(), classOrder.size());
        for (const int index : ordered) {
            const auto row = index < records.size() ? records[index].toMap() : QVariantMap{};
            const auto classification = exportGroupKey(row);
            auto rank = classOrder.indexOf(exportClassKey(classification,classOrder));
            if(rank<0)rank=classOrder.indexOf(QStringLiteral("unknown"));
            if (rank >= 0) ranks[index] = rank;
        }
        std::stable_sort(ordered.begin(), ordered.end(), [&](int a, int b) { return ranks[a] < ranks[b]; });
    }
    return ordered;
}
}
