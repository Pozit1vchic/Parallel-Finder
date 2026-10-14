#pragma once

#include <pfcore/MotionMatcher.hpp>
#include <QStringList>
#include <QVariantList>
#include <algorithm>
#include <tuple>
#include <vector>

namespace pfui {
inline QString exportClassKey(const QString& value,const QStringList& classOrder) {
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
        return std::tie(a.leftSourceId, a.leftStartSeconds, a.rightStartSeconds)
             < std::tie(b.leftSourceId, b.leftStartSeconds, b.rightStartSeconds);
    });
    if (!classOrder.isEmpty()) {
        std::vector<qsizetype> ranks(matches.size(), classOrder.size());
        for (const int index : ordered) {
            const auto classification = index < records.size()
                ? records[index].toMap().value("classification", "unknown").toString().section('/', 0, 0) : QStringLiteral("unknown");
            auto rank = classOrder.indexOf(exportClassKey(classification,classOrder));
            if(rank<0)rank=classOrder.indexOf(QStringLiteral("unknown"));
            if (rank >= 0) ranks[index] = rank;
        }
        std::stable_sort(ordered.begin(), ordered.end(), [&](int a, int b) { return ranks[a] < ranks[b]; });
    }
    return ordered;
}
}
