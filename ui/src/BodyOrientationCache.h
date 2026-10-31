#pragma once
#include <pfcore/BodyOrientationSample.hpp>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdint>

namespace pfui::body_detail {
inline std::vector<std::uint8_t> serialize(const pfcore::BodyOrientationSample& sample) {
    const auto bytes=QJsonDocument(QJsonObject{{"schema",1},{"source",QString::fromStdString(sample.sourceId)},
        {"model",QString::fromStdString(sample.modelHash)},{"time",sample.time},{"observed",sample.observed},
        {"poseAssociated",sample.poseAssociated},{"reason",QString::fromStdString(sample.reason)},
        {"yaw",sample.observed?QJsonValue(sample.yawDegrees):QJsonValue::Null},
        {"iou",sample.associationIou},{"margin",sample.associationMargin},
        {"mirror",sample.mirrorDifference},{"crop",sample.cropDifference}}).toJson(QJsonDocument::Compact);
    return {reinterpret_cast<const std::uint8_t*>(bytes.constData()),reinterpret_cast<const std::uint8_t*>(bytes.constData())+bytes.size()};
}
inline std::optional<pfcore::BodyOrientationSample> read(const std::vector<std::uint8_t>& bytes,
    const std::string& source,double time) {
    if(bytes.empty()||bytes.size()>65536)return {};
    QJsonParseError error;const auto doc=QJsonDocument::fromJson(QByteArray(reinterpret_cast<const char*>(bytes.data()),bytes.size()),&error);
    if(error.error!=QJsonParseError::NoError||!doc.isObject())return {};
    const auto value=doc.object();
    if(value["schema"].toInt()!=1||value["source"].toString().toStdString()!=source||!value["time"].isDouble()
        ||value["time"].toDouble()!=time||value["model"].toString()!=QString::fromLatin1(pfcore::kBodyOrientationModelHash)
        ||!value["observed"].isBool()||!value["poseAssociated"].isBool()||!value["reason"].isString())return {};
    pfcore::BodyOrientationSample s;s.sourceId=source;s.time=time;s.modelHash=pfcore::kBodyOrientationModelHash;
    s.observed=value["observed"].toBool();s.poseAssociated=value["poseAssociated"].toBool();s.reason=value["reason"].toString().toStdString();
    if(s.reason.size()>128)return {};
    for(const auto key:{"iou","margin","mirror","crop"})if(!value[key].isDouble()||!std::isfinite(value[key].toDouble()))return {};
    s.associationIou=value["iou"].toDouble();s.associationMargin=value["margin"].toDouble();
    s.mirrorDifference=value["mirror"].toDouble();s.cropDifference=value["crop"].toDouble();
    if(s.observed) {
        if(!value["yaw"].isDouble())return {};
        s.yawDegrees=value["yaw"].toDouble();if(!pfcore::validBodyOrientationSample(s,source))return {};
    } else if(!value["yaw"].isNull())return {};
    return s;
}
}
