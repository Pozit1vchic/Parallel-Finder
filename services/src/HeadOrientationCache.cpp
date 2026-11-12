#include <pfservices/HeadOrientationCache.hpp>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <cmath>

namespace pfservices {
std::string headOrientationCacheKey(const std::string& source,const pfcore::PoseFrame& frame,
    const std::string& sourceNamespace,const std::string& modelIdentity,int providerCode) {
    QJsonArray points;
    for(const auto& p:frame.keypoints)points.append(QJsonArray{p.x,p.y,p.confidence});
    const auto data=QJsonDocument(QJsonObject{{"source",QString::fromStdString(source)},
        {"namespace",QString::fromStdString(sourceNamespace)},{"time",frame.timestampSeconds},
        {"pose",points},{"model",QString::fromStdString(modelIdentity)},
        {"provider",providerCode}}).toJson(QJsonDocument::Compact);
    return "head-orientation-v1|PIL-resize256-center224|unaligned-yunet-nose|fp32|"
        +QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex().toStdString();
}
std::optional<pfcore::HeadOrientationSample> deserializeHeadOrientationSample(
    const std::vector<std::uint8_t>& bytes,double expectedTime) {
    QJsonParseError error;
    const auto document=QJsonDocument::fromJson(QByteArray(reinterpret_cast<const char*>(bytes.data()),bytes.size()),&error);
    if(error.error!=QJsonParseError::NoError || !document.isObject())return {};
    const auto object=document.object();
    if(!object["observed"].isBool() || !object["time"].isDouble() || object["time"].toDouble()!=expectedTime)return {};
    pfcore::HeadOrientationSample sample{expectedTime,{},object["observed"].toBool()};
    const auto rotation=object["rotation"].toArray();
    if(sample.observed && rotation.size()!=9)return {};
    if(sample.observed)for(int i=0;i<9;++i) {
        if(!rotation[i].isDouble() || !std::isfinite(rotation[i].toDouble()))return {};
        sample.rotation[i]=rotation[i].toDouble();
    }
    return sample;
}
std::vector<std::uint8_t> serializeHeadOrientationSample(const pfcore::HeadOrientationSample& sample) {
    QJsonArray matrix;if(sample.observed)for(const auto v:sample.rotation)matrix.append(v);
    const auto bytes=QJsonDocument(QJsonObject{{"time",sample.time},{"observed",sample.observed},{"rotation",matrix}}).toJson(QJsonDocument::Compact);
    return {reinterpret_cast<const std::uint8_t*>(bytes.constData()),reinterpret_cast<const std::uint8_t*>(bytes.constData())+bytes.size()};
}
}
