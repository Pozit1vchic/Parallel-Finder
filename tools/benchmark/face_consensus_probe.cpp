#include <pfcore/FaceConsensus.hpp>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>

int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);
    if(app.arguments().size()!=2)return 2;
    QFile file(app.arguments()[1]);if(!file.open(QIODevice::ReadOnly))return 2;
    QJsonParseError error;const auto input=QJsonDocument::fromJson(file.readAll(),&error);
    if(error.error!=QJsonParseError::NoError || !input.isArray())return 2;
    struct Group {QJsonObject metadata;std::vector<pfcore::DirectFaceSample> samples;};
    std::vector<Group> groups;
    for(const auto& entry:input.array()) {
        auto object=entry.toObject();Group group{object,{}};
        for(const auto& value:object["samples"].toArray()) {
            auto sample=value.toObject();pfcore::DirectFaceSample face;
            face.timestampSeconds=sample["time"].toDouble();
            for(const auto& element:sample["face"].toArray())face.embedding.push_back(element.toDouble());
            group.samples.push_back(std::move(face));
        }
        groups.push_back(std::move(group));
    }
    QJsonArray output;
    for(std::size_t a=0;a<groups.size();++a)for(std::size_t b=a+1;b<groups.size();++b) {
        const auto& left=groups[a];const auto& right=groups[b];
        auto result=pfcore::compareDirectFaces(left.samples,left.metadata["start"].toDouble(),left.metadata["end"].toDouble(),
            right.samples,right.metadata["start"].toDouble(),right.metadata["end"].toDouble());
        const char* decision=result.decision==pfcore::FaceConsensusDecision::Supported?"supported":
            result.decision==pfcore::FaceConsensusDecision::Contradicted?"contradicted":"insufficient";
        output.append(QJsonObject{{"a",left.metadata["id"]},{"b",right.metadata["id"]},
            {"samePerson",left.metadata.contains("identity") && right.metadata.contains("identity")
                && left.metadata["identity"].isString() && right.metadata["identity"].isString()
                && !left.metadata["identity"].toString().isEmpty() && !right.metadata["identity"].toString().isEmpty()
                ? QJsonValue(left.metadata["identity"]==right.metadata["identity"]) : QJsonValue(QJsonValue::Null)},
            {"decision",QString::fromLatin1(decision)},
            {"meanSupportedSimilarity",result.meanSupportedSimilarity},
            {"leftIndependent",qint64(result.leftIndependent)},{"rightIndependent",qint64(result.rightIndependent)},
            {"leftSupported",qint64(result.leftSupported)},{"rightSupported",qint64(result.rightSupported)}});
    }
    std::cout<<QJsonDocument(output).toJson(QJsonDocument::Compact).constData()<<'\n';
}
