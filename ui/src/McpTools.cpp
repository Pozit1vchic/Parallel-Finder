#include "McpController.h"
#include "AnalysisController.h"
#include <pfcore/VideoDecoder.hpp>
#include <QBuffer>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QSet>
#include <QUrl>
#include <cmath>
#include <stdexcept>

namespace pfui {
namespace {
[[noreturn]] void reject(const QString& message){throw std::runtime_error(message.toStdString());}
QJsonObject kind(const char* type){return {{"type",type}};}
QJsonObject choice(const QStringList& values){return {{"type","string"},{"enum",QJsonArray::fromStringList(values)}};}
QJsonObject bounded(const char* type,double lo,double hi){return {{"type",type},{"minimum",lo},{"maximum",hi}};}
QJsonObject object(const QJsonObject& properties,const QJsonArray& required={}){return {{"type","object"},{"properties",properties},{"required",required},{"additionalProperties",false}};}
QJsonObject settingsSchema(){return object({
    {"providerChoice",choice({"auto","cpu","cuda","dml","tensorrt"})},
    {"qualityProfile",choice({"fast","medium","maximum"})},{"analysisMode",choice({"motion","static","combined"})},
    {"normalizeSize",kind("boolean")},{"mirrorPoses",kind("boolean")},{"expandedSearch",kind("boolean")},
    {"processingThreads",bounded("integer",0,256)},{"sceneThreshold",bounded("number",1,255)},
    {"similarityThreshold",bounded("number",0,1)},{"candidateThreshold",bounded("number",0,1)},
    {"repeatGap",bounded("number",0,30)},{"sameFileGap",bounded("number",0,15)},{"crossFileGap",bounded("number",0,15)},
    {"duplicateWindow",bounded("number",.25,8)},{"noiseFactor",bounded("number",0,2)},
    {"maxUniqueResults",bounded("integer",10,500)},{"timeWeight",bounded("number",0,1)}});}
void validate(const QJsonValue& value,const QJsonObject& spec,const QString& path){
    const auto type=spec["type"].toString();
    if((type=="string"&&!value.isString())||(type=="boolean"&&!value.isBool())
        ||(type=="object"&&!value.isObject())||(type=="array"&&!value.isArray())
        ||((type=="number"||type=="integer")&&(!value.isDouble()||!std::isfinite(value.toDouble()))))reject("Invalid "+path);
    if(type=="integer" && std::floor(value.toDouble())!=value.toDouble())reject("Expected integer: "+path);
    if(spec.contains("enum")&&!spec["enum"].toArray().contains(value))reject("Unsupported "+path);
    if(value.isDouble()&&((spec.contains("minimum")&&value.toDouble()<spec["minimum"].toDouble())
        ||(spec.contains("maximum")&&value.toDouble()>spec["maximum"].toDouble())))reject("Out of range: "+path);
    if(type=="object"){
        const auto properties=spec["properties"].toObject(),values=value.toObject();
        for(auto i=values.begin();i!=values.end();++i){if(!properties.contains(i.key()))reject("Unknown setting: "+i.key());validate(i.value(),properties[i.key()].toObject(),path+"."+i.key());}
    }
}
QJsonObject settings(AnalysisController* analysis){
    QJsonObject result;
    const auto properties=settingsSchema()["properties"].toObject();
    for(auto i=properties.begin();i!=properties.end();++i)result.insert(i.key(),QJsonValue::fromVariant(analysis->property(i.key().toUtf8())));
    for(const auto key:{"accuracyPreset","modelPath","modelChoice","modelStatus","cachePath","cacheLimitGb"})result.insert(key,QJsonValue::fromVariant(analysis->property(key)));
    return result;
}
void imageContent(const QString& url,QJsonArray& images,int width){
    const auto path=url.startsWith("file:")?QUrl(url).toLocalFile():url;
    QImageReader reader(path);const auto size=reader.size();
    if(!size.isValid()||qint64(size.width())*size.height()>16*1024*1024)reject("Preview is unavailable; poll status and retry after preview preparation");
    reader.setScaledSize(size.scaled(width,720,Qt::KeepAspectRatio));const auto image=reader.read();
    if(image.isNull())reject("Cannot read generated preview");
    QByteArray bytes;QBuffer buffer(&bytes);buffer.open(QIODevice::WriteOnly);
    if(!image.save(&buffer,"PNG")||bytes.size()>4*1024*1024)reject("Preview encoding failed");
    images.append(QJsonObject{{"type","image"},{"mimeType","image/png"},{"data",QString::fromLatin1(bytes.toBase64())}});
}
}
QJsonArray McpController::extendedTools() const {
    QJsonArray tools;
    const auto add=[&](const char* name,const char* description,const QJsonObject& properties={},const QJsonArray& required={},bool readOnly=true){
        if(readOnly||allowControl_)tools.append(QJsonObject{{"name",name},{"description",description},{"inputSchema",object(properties,required)},
            {"annotations",QJsonObject{{"readOnlyHint",readOnly},{"destructiveHint",false},{"openWorldHint",false}}}});
    };
    const auto id=bounded("integer",0,1000000),side=choice({"left","right"});
    add("pf_get_capabilities","Discover available commands, timing units, asynchronous behavior and result-set guards");
    add("pf_get_settings","Read effective analysis/matcher settings, model and cache location; does not expose MCP secrets");
    add("pf_get_sources","Read original source metadata and identity-admission summaries");
    add("pf_get_result","Get one result including clip bounds, classification facets and review state",{{"id",id}},{"id"});
    add("pf_get_preview","Return a generated preview as an MCP image. A still does not prove matching motion",{{"id",id},{"side",side},{"width",bounded("integer",128,1280)}},{"id","side"});
    add("pf_get_draft_preview","Read readiness and MCP image of the asynchronous draft from pf_preview_range",{{"side",side},{"width",bounded("integer",128,1280)}});
    add("pf_get_export_status","Read current export progress and last asynchronous outcome");
    add("pf_get_model_status","Read selected model, availability and download status; no downloads are started");
    add("pf_get_video_info","Read metadata of an existing absolute local video path without analyzing it",{{"path",kind("string")}},{"path"});
    add("pf_list_video_files","List local video paths in one existing folder, paginated and non-recursive",{{"folder",kind("string")},{"offset",bounded("integer",0,1000000)},{"limit",bounded("integer",1,200)}},{"folder"});
    add("pf_update_settings","Validate every requested setting before changing any. Does not clear cache or start analysis",{{"settings",settingsSchema()}},{"settings"},false);
    add("pf_apply_accuracy_preset","Apply the actual fast/balanced/precise matcher values, not just the preset label",{{"preset",choice({"fast","balanced","precise"})}},{"preset"},false);
    add("pf_set_visibility","Hide or restore results without deleting source videos or analysis cache",{{"ids",QJsonObject{{"type","array"},{"items",id}}},{"hidden",kind("boolean")}},{"ids","hidden"},false);
    add("pf_reset_clip_range","Restore original clip boundaries; retains explicit manual classification",{{"id",id}},{"id"},false);
    add("pf_replace_side","Copy an existing clip from another result into target A/B. Donor stays unchanged; undo is available",{{"targetId",id},{"targetSide",side},{"donorId",id},{"donorSide",side}},{"targetId","targetSide","donorId","donorSide"},false);
    add("pf_combine_singles","Join two single moments into one pair, hiding the donor single. One undo restores both",{{"targetId",id},{"donorId",id}},{"targetId","donorId"},false);
    add("pf_remove_side","Remove one side and retain the other as a single moment; never deletes source media",{{"id",id},{"side",side}},{"id","side"},false);
    add("pf_set_group","Assign or clear a custom editorial group without changing movement classification",{{"ids",QJsonObject{{"type","array"},{"items",id}}},{"group",kind("string")}},{"ids","group"},false);
    add("pf_undo_result_edit","Undo the last supported review edit; returns restored IDs",{},{},false);
    add("pf_preview_range","Prepare two draft starting frames asynchronously without saving new clip bounds",{{"id",id},{"leftStart",bounded("number",0,1000000)},{"rightStart",bounded("number",0,1000000)}},{"id","leftStart","rightStart"},false);
    add("pf_export_findings","Export selected visible results as PNG findings sheets; returns immediately",{{"folder",kind("string")},{"ids",QJsonObject{{"type","array"},{"items",id}}},{"framesPerSide",QJsonObject{{"type","integer"},{"enum",QJsonArray{1,3,5}}}}},{"folder","ids"},false);
    add("pf_select_available_model","Select an already installed pose model by filename; never downloads a missing model",{{"filename",kind("string")}},{"filename"},false);
    return tools;
}
QJsonObject McpController::callExtendedTool(const QString& name,const QJsonObject& args,QJsonArray& images){
    QJsonObject spec,annotations;
    for(const auto& entry:extendedTools())if(entry.toObject()["name"]==name){spec=entry.toObject()["inputSchema"].toObject();annotations=entry.toObject()["annotations"].toObject();}
    auto checked=args;checked.remove("resultSet");validate(checked,spec,"arguments");
    const bool readOnly=annotations["readOnlyHint"].toBool();
    if(!readOnly && (!allowControl_||analysis_->busy()||analysis_->exportBusy()||analysis_->reviewBusy()))reject("Control is disabled or application is busy; poll pf_get_status");
    const auto row=[&](){for(const auto& value:analysis_->results())if(value.toMap()["id"].toInt()==args["id"].toInt(-1))return value.toMap();reject("Unknown result ID");};
    const auto ids=[&](){const auto input=args["ids"].toArray();if(input.isEmpty()||input.size()>1000)reject("Expected 1–1000 unique result IDs");QVariantList result;QSet<int> seen;
        for(const auto& value:input){if(!value.isDouble()||value.toDouble()!=value.toInt(-1)||value.toInt(-1)<0||seen.contains(value.toInt()))reject("Invalid or duplicate result ID");
            const int id=value.toInt();bool found=false;for(const auto& record:analysis_->results())if(record.toMap()["id"].toInt()==id)found=true;
            if(!found)reject("Unknown result ID");
            seen.insert(id);result.append(id);}return result;};
    const auto directory=[&](){const auto path=args["folder"].toString();if(!QDir::isAbsolutePath(path)||!QFileInfo(path).isDir())reject("Expected an existing absolute folder");return path;};
    if(name=="pf_get_capabilities")return {{"tools",dispatch({{"method","tools/list"}})["result"].toObject()["tools"]},{"timeUnit","seconds"},{"longJobs","Poll pf_get_status; accepted is not completion"},{"resultSetGuard","Echo resultSet from reads in calls; refresh IDs after it changes"},{"cachePolicy","Source analysis caches are preserved; no clear-cache command"}};
    if(name=="pf_get_settings")return settings(analysis_);
    if(name=="pf_get_sources")return {{"sources",QJsonObject::fromVariantMap(analysis_->sourceSummaries())}};
    if(name=="pf_get_result")return QJsonObject::fromVariantMap(row());
    if(name=="pf_get_preview"){const auto record=row();if(record[args["side"].toString()+"Removed"].toBool())reject("This side was removed; inspect the remaining side");imageContent(record[args["side"].toString()+"Preview"].toString(),images,args["width"].toInt(960));return {{"id",args["id"]},{"side",args["side"]},{"scope","generated starting frame; not a motion assessment"}};}
    if(name=="pf_get_draft_preview"){
        if(draftPreview_.isEmpty()||analysis_->reviewBusy())return {{"ready",false}};
        if(draftPreview_.contains("error"))return draftPreview_;
        const auto side=args["side"].toString("left");imageContent(draftPreview_[side+"Preview"].toString(),images,args["width"].toInt(960));
        auto data=draftPreview_;data.insert("ready",true);data.insert("side",side);return data;
    }
    if(name=="pf_get_export_status")return {{"busy",analysis_->exportBusy()},{"completed",analysis_->exportCompleted()},{"total",analysis_->exportTotal()},{"stage",analysis_->exportStage()},{"success",exportSuccess_},{"message",exportMessage_}};
    if(name=="pf_get_model_status")return {{"path",analysis_->modelPath()},{"choice",analysis_->modelChoice()},{"status",analysis_->modelStatus()},{"downloading",analysis_->modelDownloading()},{"downloadProgress",analysis_->modelDownloadProgress()}};
    if(name=="pf_get_video_info"){
        const QFileInfo file(args["path"].toString());if(!file.isAbsolute()||!file.isFile()||!file.isReadable())reject("Video is unavailable");
        pfcore::VideoDecoder decoder;pfcore::VideoDecodeOptions options;options.threads=1;decoder.open(file.absoluteFilePath().toStdString(),options);const auto info=decoder.info();
        return {{"path",file.absoluteFilePath()},{"bytes",double(file.size())},{"width",info.width},{"height",info.height},{"fps",info.frameRate},{"durationSeconds",info.durationSeconds}};
    }
    if(name=="pf_list_video_files"){
        const auto files=analysis_->filesInFolder(directory());const int offset=args["offset"].toInt(),limit=args["limit"].toInt(100);QJsonArray page;
        for(int i=offset;i<files.size()&&page.size()<limit;++i)page.append(files[i]);
        return {{"paths",page},{"total",files.size()},{"nextOffset",offset+page.size()<files.size()?QJsonValue(offset+page.size()):QJsonValue(QJsonValue::Null)}};
    }
    if(name=="pf_update_settings"){
        const auto updates=args["settings"].toObject();if(updates.isEmpty())reject("No settings supplied");
        for(auto i=updates.begin();i!=updates.end();++i){if(i.key()=="sceneThreshold")analysis_->setSceneThreshold(i.value().toDouble());
            else if(!analysis_->setProperty(i.key().toUtf8(),i.value().toVariant()))reject("Cannot update "+i.key());}
        for(const auto& key:QStringList{"similarityThreshold","candidateThreshold","repeatGap","sameFileGap","crossFileGap","duplicateWindow","noiseFactor","maxUniqueResults","timeWeight"})
            if(updates.contains(key)){analysis_->setAccuracyPreset("custom");break;}
        return {{"accepted",true},{"settings",settings(analysis_)}};
    }
    if(name=="pf_apply_accuracy_preset"){
        const auto preset=args["preset"].toString();const bool fast=preset=="fast",precise=preset=="precise";
        analysis_->setSimilarityThreshold(fast?.70:precise?.84:.76);analysis_->setCandidateThreshold(fast?.48:precise?.68:.55);
        analysis_->setRepeatGap(fast?8:precise?4:6);analysis_->setSameFileGap(fast?3:precise?1.5:2);analysis_->setCrossFileGap(0);
        analysis_->setDuplicateWindow(fast?2:precise?1:1.5);analysis_->setNoiseFactor(fast?1.25:precise?.70:1);
        analysis_->setMaxUniqueResults(500);analysis_->setTimeWeight(fast?.10:precise?.40:.25);analysis_->setAccuracyPreset(preset);
        return {{"accepted",true},{"settings",settings(analysis_)}};
    }
    if(name=="pf_set_visibility"){const auto selected=ids();analysis_->setResultReview(selected,"hidden",args["hidden"].toBool());return {{"accepted",true},{"ids",QJsonArray::fromVariantList(selected)}};}
    if(name=="pf_replace_side"){if(!analysis_->replaceResultSide(args["targetId"].toInt(),args["targetSide"].toString(),args["donorId"].toInt(),args["donorSide"].toString()))reject("Side replacement rejected; refresh results and check active sides");return {{"accepted",true}};}
    if(name=="pf_combine_singles"){if(!analysis_->combineSingleResults(args["targetId"].toInt(),args["donorId"].toInt()))reject("Expected two distinct single moments with compatible active sides");return {{"accepted",true}};}
    if(name=="pf_remove_side"){row();if(!analysis_->removeResultSide(args["id"].toInt(),args["side"].toString()))reject("Cannot remove this side; at least one side must remain");return {{"accepted",true}};}
    if(name=="pf_set_group"){const auto selected=ids();if(!analysis_->setResultGroup(selected,args["group"].toString()))reject("Invalid group name or unavailable results");return {{"accepted",true}};}
    if(name=="pf_reset_clip_range"){row();if(!analysis_->resetResultRange(args["id"].toInt()))reject("No original range to restore or result is unavailable");}
    else if(name=="pf_undo_result_edit"){const auto restored=analysis_->undoResultEdit();return {{"accepted",true},{"restoredIds",QJsonArray::fromVariantList(restored)},{"changed",!restored.isEmpty()}};}
    else if(name=="pf_preview_range"){row();draftPreview_={};if(!analysis_->previewResultRange(args["id"].toInt(),args["leftStart"].toDouble(),args["rightStart"].toDouble()))reject("Draft bounds rejected");}
    else if(name=="pf_export_findings"){const auto selected=ids();if(!analysis_->exportFindings(directory(),selected,args["framesPerSide"].toInt(3)))reject(analysis_->status());}
    else if(name=="pf_select_available_model"){
        const auto filename=args["filename"].toString();if(filename!=QFileInfo(filename).fileName()||!analysis_->modelAvailable(filename))reject("Model is not installed; no download started");analysis_->selectModel(filename);
    } else reject("Unknown tool");
    return {{"accepted",true}};
}
}
