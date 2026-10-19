#include "McpController.h"
#include "AnalysisController.h"
#include <pfservices/SettingsStore.hpp>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRandomGenerator>
#include <QGuiApplication>
#include <QClipboard>
#include <QFileInfo>
#include <QDir>
#include <QPointer>
#include <QSet>
#include <cmath>
#include <stdexcept>

namespace pfui {
namespace {
QString freshKey(){QString value;for(int i=0;i<8;++i)value+=QString::number(QRandomGenerator::system()->generate(),16).rightJustified(8,'0');return value;}
QJsonObject rpcError(const QJsonValue& id,int code,const QString& message){return {{"jsonrpc","2.0"},{"id",id},{"error",QJsonObject{{"code",code},{"message",message}}}};}
const QStringList classes{"head_turn_away","head_turn_back","head_turn","turn","head_front","head_turned","head_pose","arms_raised","arms_extended","arms_folded","legs_bent","legs_straight","body_pose","pose_mismatch","head_nod","head_tilt","raise","lower","raise_lower","wave","push","pull","reach_return","step","static","mixed","action_mismatch","unknown"};
QJsonObject schema(const QJsonObject& properties={},const QJsonArray& required={}){return {{"type","object"},{"properties",properties},{"required",required},{"additionalProperties",false}};}
QJsonObject tool(const QString& name,const QString& description,const QJsonObject& input,bool readOnly){return {{"name",name},{"description",description},{"inputSchema",input},{"annotations",QJsonObject{{"readOnlyHint",readOnly},{"destructiveHint",name=="pf_start_analysis"},{"openWorldHint",false}}}};}
QJsonObject type(const char* value){return {{"type",value}};}
QJsonObject idsType(){return {{"type","array"},{"items",type("integer")},{"minItems",1},{"maxItems",1000},{"uniqueItems",true}};}
}
McpController::McpController(AnalysisController* analysis,const QString& settingsPath,QObject* parent)
    :QObject(parent),analysis_(analysis),settingsPath_(settingsPath),server_([this](const auto& r){return dispatch(r);},this) {
    resultSet_=freshKey();
    connect(analysis_,&AnalysisController::resultsChanged,this,[this]{resultSet_=freshKey();draftPreview_={};});
    connect(analysis_,&AnalysisController::reviewPreviewsReady,this,[this](int id,double leftStart,double rightStart,const QString& left,const QString& right){
        draftPreview_={{"id",id},{"leftStart",leftStart},{"rightStart",rightStart},{"leftPreview",left},{"rightPreview",right},{"resultSet",resultSet_}};
    });
    connect(analysis_,&AnalysisController::reviewPreviewFailed,this,[this](int id,double leftStart,double rightStart,const QString& error){
        draftPreview_={{"id",id},{"leftStart",leftStart},{"rightStart",rightStart},{"error",error},{"ready",false}};
    });
    connect(analysis_,&AnalysisController::exportBusyChanged,this,[this]{
        if(analysis_->exportBusy()){exportSuccess_=QJsonValue::Null;exportMessage_.clear();}
    });
    connect(analysis_,&AnalysisController::exportFinished,this,[this](bool success,const QString& message){
        exportSuccess_=success;exportMessage_=message;
    });
    std::string error;const auto settings=pfservices::SettingsStore(settingsPath.toStdString()).load(error);
    enabled_=settings.mcpEnabled;allowControl_=settings.mcpAllowControl;port_=settings.mcpPort;token_=QString::fromStdString(settings.mcpToken);
    if(enabled_){const bool needsKey=token_.isEmpty();apply();if(needsKey)persist();}
}
void McpController::persist(){
    const auto enabled=enabled_,control=allowControl_;const auto port=port_;const auto token=token_.toStdString();
    QPointer<McpController> guard(this);
    pfservices::SettingsStore(settingsPath_.toStdString()).updateAsync("mcp",[=](auto& s){s.mcpEnabled=enabled;s.mcpAllowControl=control;s.mcpPort=port;s.mcpToken=token;},
        [guard](std::string error){if(!error.empty() && guard)QMetaObject::invokeMethod(guard,[guard,error]{if(guard){guard->error_=QString::fromStdString(error);emit guard->changed();}},Qt::QueuedConnection);});
}
void McpController::apply(){server_.stop();error_.clear();if(enabled_){if(token_.isEmpty())token_=freshKey();server_.start(static_cast<quint16>(port_),token_.toUtf8(),error_);}emit changed();}
void McpController::setEnabled(bool value){if(enabled_==value && (running()||!value))return;enabled_=value;apply();persist();}
void McpController::setAllowControl(bool value){if(allowControl_==value)return;allowControl_=value;persist();emit changed();}
void McpController::setPort(int value){if(value<1024||value>65535||port_==value)return;port_=value;apply();persist();}
void McpController::rotateKey(){token_=freshKey();apply();persist();}
void McpController::copyConnection(){
    if(!running())return;
    const QJsonObject config{{"mcpServers",QJsonObject{{"parallel-finder",QJsonObject{{"url",endpoint()},{"headers",QJsonObject{{"Authorization","Bearer "+token_}}}}}}}};
    QGuiApplication::clipboard()->setText(QString::fromUtf8(QJsonDocument(config).toJson(QJsonDocument::Indented)));
}
QJsonObject McpController::dispatch(const QJsonObject& request){
    auto classes=pfui::classes;
    for(const auto& value:analysis_->results()) {
        const auto key=value.toMap().value("classification","unknown").toString().section('/',0,0);
        if(!key.isEmpty() && !classes.contains(key))classes.insert(classes.size()-1,key);
    }
    const auto id=request["id"];const auto method=request["method"].toString();const auto params=request["params"].toObject();
    QJsonObject result;
    if(method=="initialize") {
        const auto version=params["protocolVersion"].toString();
        result={{"protocolVersion",QStringList{"2025-06-18","2025-11-25"}.contains(version)?version:QString("2025-11-25")},
            {"capabilities",QJsonObject{{"tools",QJsonObject{}},{"resources",QJsonObject{}},{"prompts",QJsonObject{}}}},{"serverInfo",QJsonObject{{"name","parallel-finder"},{"version",QStringLiteral(PF_VERSION)}}},
            {"instructions","Read application state and grouped movements. Mutating tools require Allow control in the app. Long jobs return immediately; poll pf_get_status. Never clear analysis caches."}};
    } else if(method=="ping")result={};
    else if(method=="resources/list") {
        QJsonArray resources;
        for(const auto& key:QStringList{"status","settings","classes","capabilities"})resources.append(QJsonObject{{"uri","pf://"+key},{"name",key},{"mimeType","application/json"}});
        result={{"resources",resources}};
    } else if(method=="resources/read") {
        const auto uri=params["uri"].toString();
        const QMap<QString,QString> tools{{"pf://status","pf_get_status"},{"pf://settings","pf_get_settings"},{"pf://classes","pf_list_classes"},{"pf://capabilities","pf_get_capabilities"}};
        if(!tools.contains(uri))return rpcError(id,-32602,"Unknown resource URI");
        const auto called=dispatch({{"id",0},{"method","tools/call"},{"params",QJsonObject{{"name",tools[uri]}}}})["result"].toObject();
        result={{"contents",QJsonArray{QJsonObject{{"uri",uri},{"mimeType","application/json"},{"text",QString::fromUtf8(QJsonDocument(called["structuredContent"].toObject()).toJson(QJsonDocument::Compact))}}}}};
    } else if(method=="prompts/list")result={{"prompts",QJsonArray{QJsonObject{{"name","review-parallels"},{"description","Inspect both clips, preserve cache and organize results within the user's request"}}}}};
    else if(method=="prompts/get") {
        if(params["name"]!="review-parallels")return rpcError(id,-32602,"Unknown prompt");
        result={{"messages",QJsonArray{QJsonObject{{"role","user"},{"content",QJsonObject{{"type","text"},{"text","Read pf_get_status and pf_get_capabilities, then page pf_list_results. Inspect both sides with pf_get_preview; still images do not prove matching motion. Include resultSet in edits, respect manual classifications and preserve caches and source videos. Make only changes authorized by the user. Poll status for asynchronous jobs; accepted does not mean complete."}}}}}}};
    }
    else if(method=="tools/list") {
        QJsonArray list{
            tool("pf_get_status","Current analysis/export progress and source summaries",schema(),true),
            tool("pf_list_results","Paginated results in classification order; stable IDs for edits",schema({{"offset",type("integer")},{"limit",type("integer")},{"classification",type("string")}}),true),
            tool("pf_list_classes","Movement classes and result counts",schema(),true)};
        if(allowControl_) {
            list.append(tool("pf_set_classification","Assign a manual movement class to existing result IDs",schema({{"ids",idsType()},{"classification",QJsonObject{{"type","string"},{"enum",QJsonArray::fromStringList(classes)}}}},{"ids","classification"}),false));
            list.append(tool("pf_set_clip_range","Set matched clip bounds on both sides; app validates supported bounds",schema({{"id",type("integer")},{"leftStart",type("number")},{"leftEnd",type("number")},{"rightStart",type("number")},{"rightEnd",type("number")}},{"id","leftStart","leftEnd","rightStart","rightEnd"}),false));
            list.append(tool("pf_set_review","Set favorite/reviewed state",schema({{"ids",idsType()},{"field",QJsonObject{{"type","string"},{"enum",QJsonArray{"favorite","reviewed"}}}},{"value",type("boolean")}},{"ids","field","value"}),false));
            list.append(tool("pf_start_analysis","Start analysis of existing local video files using app settings and valid cache",schema({{"paths",QJsonObject{{"type","array"},{"items",type("string")},{"minItems",1},{"maxItems",64}}}},{"paths"}),false));
            list.append(tool("pf_cancel_analysis","Cancel current analysis without clearing cache",schema(),false));
            list.append(tool("pf_export_results","Export selected pairs/singles in group order. lossless is exact MOV preserving decoded picture/audio; exact is compressed MP4; fast copies packets with keyframe-aligned cuts. Montage needs compatible prepared streams in lossless/fast. Existing files receive suffixes. All modes use bounded parallel workers.",schema({{"ids",idsType()},{"folder",type("string")},{"prefix",type("string")},{"mode",QJsonObject{{"type","string"},{"enum",QJsonArray{"lossless","exact","fast"}}}},{"montage",type("boolean")},{"quality",type("integer")},{"bitrateKbps",type("integer")},{"classFolders",type("boolean")}},{"ids","folder","mode"}),false));
            list.append(tool("pf_cancel_export","Cancel current export",schema(),false));
        }
        for(const auto& extended:extendedTools())list.append(extended);
        for(int i=0;i<list.size();++i) {
            auto entry=list[i].toObject();auto input=entry["inputSchema"].toObject();auto properties=input["properties"].toObject();
            properties.insert("resultSet",type("string"));input.insert("properties",properties);entry.insert("inputSchema",input);list[i]=entry;
        }
        result={{"tools",list}};
    } else if(method=="tools/call") {
        const auto name=params["name"].toString();const auto args=params["arguments"].toObject();QJsonObject data;QJsonArray images;
        const auto fail=[](const QString& message){throw std::runtime_error(message.toStdString());};
        const auto number=[&](const char* key)->double{if(!args[key].isDouble()||!std::isfinite(args[key].toDouble()))fail(QString("Invalid %1").arg(key));return args[key].toDouble();};
        const auto integer=[&](const char* key,int fallback)->int{if(!args.contains(key))return fallback;const auto n=number(key);if(n!=std::floor(n)||n<0||n>1000000)fail(QString("Invalid %1").arg(key));return static_cast<int>(n);};
        const auto string=[&](const char* key)->QString{if(!args[key].isString()||args[key].toString().isEmpty())fail(QString("Missing %1").arg(key));return args[key].toString();};
        const auto ids=[&](){QVariantList values;if(!args["ids"].isArray()||args["ids"].toArray().isEmpty()||args["ids"].toArray().size()>1000)fail("Expected result IDs");
            const auto rows=analysis_->results();QSet<int> seen;
            for(const auto v:args["ids"].toArray()){const auto n=v.toDouble(-1);if(!v.isDouble()||n<0||n!=std::floor(n)||n>1000000)fail("Invalid result ID");const int wanted=static_cast<int>(n);
                if(seen.contains(wanted))fail("Duplicate result ID");
                seen.insert(wanted);bool found=false;for(const auto& row:rows)if(row.toMap()["id"].toInt()==wanted)found=true;if(!found)fail("Unknown result ID");values.append(wanted);}return values;};
        try {
            if(params.contains("arguments") && !params["arguments"].isObject())fail("Arguments must be an object");
            const auto advertised=dispatch(QJsonObject{{"id",0},{"method","tools/list"}})["result"].toObject()["tools"].toArray();
            QJsonObject inputSchema;bool known=false;
            for(const auto& value:advertised)if(value.toObject()["name"].toString()==name){known=true;inputSchema=value.toObject()["inputSchema"].toObject();break;}
            if(!known)fail(allowControl_?"Unknown tool":"Unknown tool or app control is disabled");
            const auto properties=inputSchema["properties"].toObject();
            for(auto it=args.begin();it!=args.end();++it){if(!properties.contains(it.key()))fail("Unknown argument: "+it.key());const auto expected=properties[it.key()].toObject()["type"].toString();
                if((expected=="boolean"&&!it->isBool())||(expected=="string"&&!it->isString())||(expected=="array"&&!it->isArray())||((expected=="integer"||expected=="number")&&!it->isDouble()))fail("Invalid argument: "+it.key());}
            for(const auto& required:inputSchema["required"].toArray())if(!args.contains(required.toString()))fail("Missing argument: "+required.toString());
            if(args.contains("resultSet") && args["resultSet"].toString()!=resultSet_)fail("Stale resultSet; refresh pf_get_status and pf_list_results before editing");
            bool extended=false;for(const auto& entry:extendedTools())if(entry.toObject()["name"]==name)extended=true;
            if(extended)data=callExtendedTool(name,args,images);
            else if(name=="pf_get_status")data={{"busy",analysis_->busy()},{"reviewBusy",analysis_->reviewBusy()},{"controlAllowed",allowControl_},{"completed",analysis_->analysisCompleted()},{"progress",analysis_->progress()},{"stage",analysis_->progressStage()},{"status",analysis_->status()},{"frames",double(analysis_->frameCount())},{"matches",analysis_->matchCount()},{"sources",QJsonObject::fromVariantMap(analysis_->sourceSummaries())},{"exportBusy",analysis_->exportBusy()},{"exportCompleted",analysis_->exportCompleted()},{"exportTotal",analysis_->exportTotal()},{"exportStage",analysis_->exportStage()},{"exportSuccess",exportSuccess_},{"exportMessage",exportMessage_}};
            else if(name=="pf_list_classes"){QJsonArray counts;for(const auto& c:classes){int count=0;for(const auto& row:analysis_->results())if(row.toMap()["classification"].toString().section('/',0,0)==c)++count;counts.append(QJsonObject{{"id",c},{"count",count}});}data={{"classes",counts}};}
            else if(name=="pf_list_results") {
                const int offset=integer("offset",0),limit=integer("limit",50);if(limit<1||limit>200)fail("Limit must be 1–200");
                const auto filter=args["classification"].toString();if(!filter.isEmpty()&&!classes.contains(filter))fail("Unknown movement class");
                QJsonArray page;int total=0;const auto rows=analysis_->results();
                for(const auto& c:classes)if(filter.isEmpty()||filter==c)for(const auto& row:rows)if(row.toMap()["classification"].toString().section('/',0,0)==c){if(total>=offset && page.size()<limit)page.append(QJsonObject::fromVariantMap(row.toMap()));++total;}
                data={{"results",page},{"total",total},{"nextOffset",offset+page.size()<total?QJsonValue(offset+page.size()):QJsonValue(QJsonValue::Null)}};
            } else {
                if(!allowControl_)fail("App control is disabled in Settings → MCP");
                if(name=="pf_cancel_analysis")analysis_->stopAnalysis();
                else if(name=="pf_cancel_export")analysis_->cancelExport();
                else {
                    if(analysis_->busy()||analysis_->exportBusy()||analysis_->reviewBusy())fail("Application is busy; poll pf_get_status");
                    if(name=="pf_set_classification"){const auto c=string("classification");if(!classes.contains(c))fail("Unknown movement class");analysis_->setResultClassification(ids(),c);}
                    else if(name=="pf_set_review"){const auto field=string("field");if((field!="favorite"&&field!="reviewed")||!args["value"].isBool())fail("Invalid review state");analysis_->setResultReview(ids(),field,args["value"].toBool());}
                    else if(name=="pf_set_clip_range"){const auto resultId=integer("id",-1);if(resultId<0||!analysis_->setResultRange(resultId,number("leftStart"),number("leftEnd"),number("rightStart"),number("rightEnd")))fail("Clip bounds were rejected");}
                    else if(name=="pf_start_analysis"){
                        if(!args["paths"].isArray()||args["paths"].toArray().isEmpty()||args["paths"].toArray().size()>64)fail("Expected local video paths");
                        QStringList paths;
                        for(const auto& item:args["paths"].toArray()){if(!item.isString())fail("Invalid file path");QFileInfo info(item.toString());if(!info.isAbsolute()||!info.isFile()||!info.isReadable())fail("Video file is unavailable");paths.append(info.absoluteFilePath());}
                        analysis_->analyzeFiles(paths);if(!analysis_->busy())fail(analysis_->status());
                    } else if(name=="pf_export_results"){
                        const auto folder=string("folder"),mode=string("mode");if(!QDir(folder).isAbsolute()||!QFileInfo(folder).isDir())fail("Export folder must already exist");if(mode!="lossless"&&mode!="exact"&&mode!="fast")fail("Invalid export mode");
                        const auto prefix=args["prefix"].toString("parallel");
                        if(prefix.isEmpty()||prefix=="."||prefix==".."||prefix.size()>64)fail("Invalid export prefix");
                        for(const auto c:prefix)if(!c.isLetterOrNumber()&&c!='_'&&c!='-'&&c!='.')fail("Invalid export prefix");
                        const auto quality=integer("quality",14),bitrate=integer("bitrateKbps",0);
                        if(quality<1||quality>51||bitrate>500000||(mode!="exact"&&bitrate))fail("Invalid quality/bitrate for this mode");
                        if(!analysis_->exportResults("FFMPEG",1,mode=="lossless"?2:mode=="fast"?1:0,folder,prefix,ids(),args["montage"].toBool(),classes,quality,bitrate,args["classFolders"].toBool(true)))fail(analysis_->status());
                    } else return rpcError(id,-32601,"Unknown tool");
                }
                data={{"accepted",true}};
            }
            data.insert("resultSet",resultSet_);
            QJsonArray content{QJsonObject{{"type","text"},{"text",QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact))}}};
            for(const auto& image:images)content.append(image);
            result={{"content",content},{"structuredContent",data},{"isError",false}};
        } catch(const std::exception& error){result={{"content",QJsonArray{QJsonObject{{"type","text"},{"text",QString::fromUtf8(error.what())}}}},{"isError",true}};}
    } else return rpcError(id,-32601,"Method not found");
    return {{"jsonrpc","2.0"},{"id",id},{"result",result}};
}
}
