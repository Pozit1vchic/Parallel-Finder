#pragma once

#include <pfcore/MotionMatcher.hpp>
#include <QStringList>
#include <QVariantList>
#include <algorithm>
#include <tuple>
#include <vector>

namespace pfui {
// One ordering shared by video and metadata exports. Chronological merge is
// handled by the cutter separately; it does not change individual clip names.
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
            auto rank = classOrder.indexOf(classification);
            if(rank<0)rank=classOrder.indexOf(QStringLiteral("unknown"));
            if (rank >= 0) ranks[index] = rank;
        }
        std::stable_sort(ordered.begin(), ordered.end(), [&](int a, int b) { return ranks[a] < ranks[b]; });
    }
    return ordered;
}
}
