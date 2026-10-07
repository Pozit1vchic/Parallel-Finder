#pragma once
#include <QVariantMap>

namespace pfservices {
// Stable tags for an exact pair, independent of UI row order and A/B order.
// Source size/mtime prevent tags leaking onto a replaced video at the same path.
class ResultCategoryStore {
public:
    static QString key(const QVariantMap& record);
    static QVariantMap load(const QVariantMap& record);
    static void save(const QVariantMap& record, const QString& name, const QString& color);
};
}
