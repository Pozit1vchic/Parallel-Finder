#include <pfcore/SceneDescriptor.hpp>
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <iostream>

// Frozen neural observations plus independently reviewed source-interval labels.
// No inference, training, source mutation or fabricated missing observations.
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);const auto args=app.arguments();
    const bool verify=args.size()==3&&args[2]=="--verify";
    if(args.size()!=2&&!verify){std::cerr<<"usage: pf_posture_probe REVIEWED_FIXTURE.json [--verify]\n";return 2;}
    QFile file(args[1]);if(!file.open(QIODevice::ReadOnly))return 2;
    QJsonParseError error;const auto input=QJsonDocument::fromJson(file.readAll(),&error);
    if(error.error!=QJsonParseError::NoError || !input.isObject() || input.object()["schema"].toInt()!=1
        || !input.object()["cases"].isArray() || input.object()["cases"].toArray().isEmpty())return 2;
    QJsonArray output;int beforeExact=0,afterExact=0,beforeCompatible=0,afterCompatible=0;
    for(const auto& value:input.object()["cases"].toArray()) {
        const auto row=value.toObject(),o=row["evidence"].toObject();
        if(!row["expectedPosture"].isString() || row["review"].toString().isEmpty())return 2;
        const auto expected=row["expectedPosture"].toString();
        if(expected!="standing" && expected!="sitting" && expected!="lying")return 2;
        pfcore::VideoActionEvidence evidence;
        evidence.sourceId=o["source"].toString().toStdString();evidence.modelHash=o["model"].toString().toStdString();
        evidence.start=o["start"].toDouble();evidence.end=o["end"].toDouble();
        for(const auto key:{"observed","continuous","associated","torsoVisible","postureVisible"})
            if(!o[key].isBool())return 2;
        evidence.observed=o["observed"].toBool();evidence.continuous=o["continuous"].toBool();
        evidence.associated=o["associated"].toBool();evidence.torsoVisible=o["torsoVisible"].toBool();
        evidence.postureVisible=o["postureVisible"].toBool();
        for(const auto time:o["times"].toArray())evidence.frameTimes.push_back(time.toDouble());
        const auto scores=o["scores"].toArray(),frozen=o["frozen"].toArray();
        if(scores.size()!=80 || frozen.size()!=80)return 2;
        for(int i=0;i<80;++i) {
            if(!scores[i].isDouble() || !frozen[i].isDouble())return 2;
            evidence.scores[i]=scores[i].toDouble();evidence.frozenScores[i]=frozen[i].toDouble();
        }
        if(!pfcore::validVideoActionEvidence(evidence,evidence.sourceId,evidence.start,evidence.end,false))return 2;
        const auto before=QString::fromStdString(pfcore::measuredPosture(evidence,evidence.sourceId,evidence.start,evidence.end));
        pfcore::MotionWindow window;window.sourceId=evidence.sourceId;window.videoActions={evidence};
        const auto descriptor=pfcore::describeScene(window,evidence.start,evidence.end);
        const auto after=descriptor.posture==pfcore::ScenePosture::Standing?QStringLiteral("standing")
            :descriptor.posture==pfcore::ScenePosture::Sitting?QStringLiteral("sitting")
            :descriptor.posture==pfcore::ScenePosture::Lying?QStringLiteral("lying")
            :descriptor.posture==pfcore::ScenePosture::SeatedOrLying?QStringLiteral("seated_lying"):QStringLiteral("unknown");
        if(verify&&before!=row["baselineBroadPosture"].toString())return 1;
        beforeExact+=before==expected;afterExact+=after==expected;
        const auto compatible=[&](const QString& label){return label==expected
            || (label=="seated_lying" && (expected=="sitting"||expected=="lying"));};
        beforeCompatible+=compatible(before);afterCompatible+=compatible(after);
        output.append(QJsonObject{{"id",row["id"]},{"expected",expected},{"beforeBroad",before},{"afterSpecific",after}});
    }
    std::cout<<QJsonDocument(QJsonObject{{"scope",input.object()["scope"]},{"cases",output},
        {"count",output.size()},{"beforeExact",beforeExact},{"afterExact",afterExact},
        {"beforeCompatible",beforeCompatible},{"afterCompatible",afterCompatible}}).toJson().constData();
    return verify&&(afterExact!=output.size()||beforeCompatible!=output.size())?1:0;
}
