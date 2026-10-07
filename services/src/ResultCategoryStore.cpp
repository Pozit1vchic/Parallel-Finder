#include <pfservices/ResultCategoryStore.hpp>
#include <QCryptographicHash>
#include <QDateTime>
#include <QFileInfo>
#include <QSettings>
#include <QStringList>

namespace pfservices {
QString ResultCategoryStore::key(const QVariantMap& record)
{
    QStringList sides;
    for(const auto& side : {QStringLiteral("left"),QStringLiteral("right")}) {
        const auto source=record.value(side+"Source").toString();
        const QFileInfo file(source);
        sides.push_back(source+"|"+QString::number(file.size())+"|"
            +QString::number(file.lastModified().toMSecsSinceEpoch())+"|"
            +QString::number(record.value(side+"Start").toDouble(),'f',6)+"|"
            +QString::number(record.value(side+"End").toDouble(),'f',6));
    }
    sides.sort();
    return QStringLiteral("resultCategories/")+QString::fromLatin1(
        QCryptographicHash::hash(sides.join('\n').toUtf8(),QCryptographicHash::Sha256).toHex());
}
QVariantMap ResultCategoryStore::load(const QVariantMap& record)
{
    return QSettings{}.value(key(record)).toMap();
}
void ResultCategoryStore::save(const QVariantMap& record,const QString& name,const QString& color)
{
    QSettings tags;
    if(name.isEmpty())tags.remove(key(record));
    else tags.setValue(key(record),QVariantMap{{"name",name},{"color",color}});
}
}
