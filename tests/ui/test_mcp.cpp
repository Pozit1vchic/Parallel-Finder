#include <QtTest/QTest>
#include <QGuiApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTcpSocket>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QProcess>
#include <QStandardPaths>
#include <QDirIterator>
#include <QScopeGuard>
#include <QCryptographicHash>
#include <QtEndian>
#include <pfservices/MotionWindowCache.hpp>
#include <AppInfo.h>
#include <pfservices/SettingsStore.hpp>
#include <pfservices/McpHttpServer.hpp>
#include <AnalysisController.h>
#include <McpController.h>

namespace {
QByteArray exchange(quint16 port,const QByteArray& bytes){
    QTcpSocket socket;socket.connectToHost(QHostAddress::LocalHost,port);
    if(!socket.waitForConnected(1000))return {};
    socket.write(bytes);socket.flush();QByteArray result;QElapsedTimer clock;clock.start();
    while(clock.elapsed()<3000){QCoreApplication::processEvents();result+=socket.readAll();if(socket.state()==QAbstractSocket::UnconnectedState)break;QTest::qWait(1);}return result;
}
QByteArray request(quint16 port,const QByteArray& key,const QByteArray& body,const QByteArray& extra={}){
    return "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1:"+QByteArray::number(port)+"\r\nAuthorization: Bearer "+key+
        "\r\nContent-Type: application/json\r\nAccept: application/json, text/event-stream\r\nMCP-Protocol-Version: 2025-11-25\r\n"+extra+"Content-Length: "+QByteArray::number(body.size())+"\r\n\r\n"+body;
}
QJsonObject message(const char* method,const QJsonObject& params={}){return {{"jsonrpc","2.0"},{"id",1},{"method",method},{"params",params}};}
QJsonObject body(const QByteArray& response){return QJsonDocument::fromJson(response.mid(response.indexOf("\r\n\r\n")+4)).object();}
}
class McpTests:public QObject {
    Q_OBJECT
private slots:
    void initTestCase(){QCoreApplication::setOrganizationName("ParallelFinderTests");QCoreApplication::setApplicationName("McpAudit");QStandardPaths::setTestModeEnabled(true);}
    void authenticatedLifecycle(){
        pfservices::McpHttpServer server([](const auto& r){return QJsonObject{{"jsonrpc","2.0"},{"id",r["id"]},{"result",QJsonObject{{"alive",true}}}};});QString error;const QByteArray key(64,'a');
        QVERIFY(server.start(0,key,error));QVERIFY(server.running());const auto port=server.port();
        const auto reply=exchange(port,request(port,key,QJsonDocument(message("ping")).toJson()));
        QVERIFY(reply.startsWith("HTTP/1.1 200"));QVERIFY(body(reply)["result"].toObject()["alive"].toBool());
        server.stop();QVERIFY(!server.running());QTcpSocket client;client.connectToHost(QHostAddress::LocalHost,port);QVERIFY(!client.waitForConnected(200));
    }
    void rejectsWrongKeyOriginAndHost(){
        int calls=0;pfservices::McpHttpServer server([&](const auto&){++calls;return QJsonObject{};});QString error;QByteArray key(64,'b');QVERIFY(server.start(0,key,error));const auto port=server.port();const auto json=QJsonDocument(message("ping")).toJson();
        QVERIFY(exchange(port,request(port,QByteArray(64,'c'),json)).startsWith("HTTP/1.1 401"));
        QVERIFY(exchange(port,request(port,key,json,"Origin: https://untrusted.example\r\n")).startsWith("HTTP/1.1 403"));
        auto evil=request(port,key,json);evil.replace("Host: 127.0.0.1:","Host: untrusted.example:");QVERIFY(exchange(port,evil).startsWith("HTTP/1.1 403"));QCOMPARE(calls,0);
    }
    void framingIsBoundedAndUnambiguous(){
        int calls=0;pfservices::McpHttpServer server([&](const auto&){++calls;return QJsonObject{};});QString error;QByteArray key(64,'d');QVERIFY(server.start(0,key,error));const auto port=server.port();const auto json=QJsonDocument(message("ping")).toJson();
        QVERIFY(exchange(port,request(port,key,"[1,2]")).startsWith("HTTP/1.1 400"));
        QVERIFY(exchange(port,request(port,key,json,"Content-Length: 10\r\n")).startsWith("HTTP/1.1 400"));
        QVERIFY(exchange(port,request(port,key,json,"Transfer-Encoding: chunked\r\n")).startsWith("HTTP/1.1 400"));
        auto oversized=request(port,key,json);oversized.replace("Content-Length: "+QByteArray::number(json.size()),"Content-Length: 1048577");QVERIFY(exchange(port,oversized).startsWith("HTTP/1.1 413"));
        auto unknown=request(port,key,json);unknown.replace("2025-11-25","2099-01-01");QVERIFY(exchange(port,unknown).startsWith("HTTP/1.1 400"));QCOMPARE(calls,0);
    }
    void notificationsAndUnsupportedGet(){
        int calls=0;pfservices::McpHttpServer server([&](const auto&){++calls;return QJsonObject{};});QString error;QByteArray key(64,'e');QVERIFY(server.start(0,key,error));const auto port=server.port();
        auto notification=message("notifications/initialized");notification.remove("id");QVERIFY(exchange(port,request(port,key,QJsonDocument(notification).toJson())).startsWith("HTTP/1.1 202"));
        auto get=request(port,key,{});get.replace("POST /mcp","GET /mcp");QVERIFY(exchange(port,get).startsWith("HTTP/1.1 405"));QCOMPARE(calls,0);
    }
    void unavailablePortDoesNotClaimRunning(){
        QTcpServer occupied;QVERIFY(occupied.listen(QHostAddress::LocalHost,0));pfservices::McpHttpServer server([](const auto&){return QJsonObject{};});QString error;
        QVERIFY(!server.start(occupied.serverPort(),QByteArray(64,'f'),error));QVERIFY(!error.isEmpty());QVERIFY(!server.running());
    }
    void appToolsHonorControlAndValidation(){
        QTemporaryDir dir;pfui::McpController controller(pfui::AnalysisController::instance(),dir.filePath("settings.json"));
        auto invoke=[&](const QString& name,const QJsonObject& args=QJsonObject{}){return controller.dispatch(message("tools/call",{{"name",name},{"arguments",args}}))["result"].toObject();};
        QCOMPARE(controller.dispatch(message("tools/list"))["result"].toObject()["tools"].toArray().size(),13);
        QVERIFY(!invoke("pf_get_status")["isError"].toBool());QVERIFY(!invoke("pf_list_results")["isError"].toBool());
        QVERIFY(invoke("pf_cancel_analysis")["isError"].toBool());QVERIFY(invoke("pf_list_results",{{"limit",201}})["isError"].toBool());QVERIFY(invoke("pf_get_status",{{"unexpected",true}})["isError"].toBool());
        controller.setAllowControl(true);QCOMPARE(controller.dispatch(message("tools/list"))["result"].toObject()["tools"].toArray().size(),32);
        QVERIFY(invoke("pf_set_classification",{{"ids",QJsonArray{999999}},{"classification","turn"}})["isError"].toBool());
        QVERIFY(invoke("pf_set_classification",{{"ids",QJsonArray{0}},{"classification","invented"}})["isError"].toBool());
        QVERIFY(invoke("pf_start_analysis",{{"paths",QJsonArray{"missing.mp4"}}})["isError"].toBool());
        QVERIFY(invoke("pf_export_results",{{"ids",QJsonArray{0}},{"folder","relative"},{"mode","lossless"}})["isError"].toBool());
        QVERIFY(!pfui::AnalysisController::instance()->busy());QVERIFY(!pfui::AnalysisController::instance()->exportBusy());
        pfservices::SettingsStore::flushPendingWrites();
    }
    void reportsAsynchronousExportOutcome(){
        QTemporaryDir dir;auto* analysis=pfui::AnalysisController::instance();
        pfui::McpController controller(analysis,dir.filePath("settings.json"));
        const auto status=[&]{return controller.dispatch(message("tools/call",{{"name","pf_get_status"}}))["result"].toObject()["structuredContent"].toObject();};
        QVERIFY(status()["exportSuccess"].isNull());
        QVERIFY(QMetaObject::invokeMethod(analysis,"exportFinished",Q_ARG(bool,false),Q_ARG(QString,QString("Test encoder failure"))));
        QCOMPARE(status()["exportSuccess"].toBool(true),false);
        QCOMPARE(status()["exportMessage"].toString(),QString("Test encoder failure"));
        QVERIFY(QMetaObject::invokeMethod(analysis,"exportFinished",Q_ARG(bool,true),Q_ARG(QString,QString("Test export completed"))));
        QCOMPARE(status()["exportSuccess"].toBool(false),true);
        QCOMPARE(status()["exportMessage"].toString(),QString("Test export completed"));
    }
    void extendedDiscoverySettingsAndStaleIds(){
        QTemporaryDir dir;auto* analysis=pfui::AnalysisController::instance();
        pfui::McpController controller(analysis,dir.filePath("settings.json"));
        const auto invoke=[&](const QString& name,const QJsonObject& args=QJsonObject{}){return controller.dispatch(message("tools/call",{{"name",name},{"arguments",args}}))["result"].toObject();};
        const auto original=invoke("pf_get_settings")["structuredContent"].toObject();
        QVERIFY(original.contains("cachePath"));QVERIFY(!original.contains("mcpToken"));
        QVERIFY(!invoke("pf_get_capabilities")["isError"].toBool());
        QVERIFY(invoke("pf_update_settings",{{"settings",QJsonObject{{"maxUniqueResults",100}}}})["isError"].toBool());
        controller.setAllowControl(true);
        const auto invalid=invoke("pf_update_settings",{{"settings",QJsonObject{{"maxUniqueResults",100},{"qualityProfile","invented"}}}});
        QVERIFY(invalid["isError"].toBool());QCOMPARE(analysis->maxUniqueResults(),original["maxUniqueResults"].toInt());
        QVERIFY(invoke("pf_update_settings",{{"settings",QJsonObject{{"processingThreads",2.5}}}})["isError"].toBool());
        QVERIFY(invoke("pf_update_settings",{{"settings",QJsonObject{{"cachePath","unapproved"}}}})["isError"].toBool());
        QVERIFY(invoke("pf_list_video_files",{{"folder",dir.path()},{"limit",0}})["isError"].toBool());
        QVERIFY(invoke("pf_select_available_model",{{"filename","../missing.onnx"}})["isError"].toBool());
        const auto before=invoke("pf_get_status")["structuredContent"].toObject()["resultSet"].toString();QVERIFY(!before.isEmpty());
        QVERIFY(QMetaObject::invokeMethod(analysis,"resultsChanged"));
        QVERIFY(invoke("pf_undo_result_edit",{{"resultSet",before}})["isError"].toBool());
        const auto now=invoke("pf_get_status")["structuredContent"].toObject()["resultSet"].toString();QVERIFY(now!=before);
        QVERIFY(!invoke("pf_undo_result_edit",{{"resultSet",now}})["isError"].toBool());
        QVERIFY(!invoke("pf_get_draft_preview")["structuredContent"].toObject()["ready"].toBool());
        QVERIFY(QMetaObject::invokeMethod(analysis,"reviewPreviewFailed",Q_ARG(int,0),Q_ARG(double,0.0),Q_ARG(double,0.0),Q_ARG(QString,QStringLiteral("Test preview failure"))));
        const auto failedDraft=invoke("pf_get_draft_preview")["structuredContent"].toObject();
        QVERIFY(!failedDraft["ready"].toBool());QCOMPARE(failedDraft["error"].toString(),QString("Test preview failure"));
        const auto init=controller.dispatch(message("initialize"))["result"].toObject();QVERIFY(init["capabilities"].toObject().contains("resources"));
        QCOMPARE(controller.dispatch(message("resources/list"))["result"].toObject()["resources"].toArray().size(),4);
        const auto resource=controller.dispatch(message("resources/read",{{"uri","pf://settings"}}))["result"].toObject()["contents"].toArray();
        QCOMPARE(resource.size(),1);QVERIFY(QJsonDocument::fromJson(resource[0].toObject()["text"].toString().toUtf8()).object().contains("modelChoice"));
        QVERIFY(controller.dispatch(message("resources/read",{{"uri","file:///secrets"}})).contains("error"));
        QVERIFY(controller.dispatch(message("prompts/get",{{"name","review-parallels"}}))["result"].toObject().contains("messages"));
        QVERIFY(controller.dispatch(message("prompts/get",{{"name","invented"}})).contains("error"));
        pfservices::SettingsStore::flushPendingWrites();
    }
    void settingsKeyRotationAndRestart(){
        QTemporaryDir dir;const auto path=dir.filePath("settings.json");std::string error;pfservices::SettingsStore store(path.toStdString());auto s=store.load(error);s.language="ru";s.cachePath="D:/preserve-cache";QVERIFY(store.save(s,error));
        QTcpServer unused;QVERIFY(unused.listen(QHostAddress::LocalHost,0));const int port=unused.serverPort();unused.close();
        QByteArray firstKey;
        {pfui::McpController controller(pfui::AnalysisController::instance(),path);controller.setPort(port);controller.setEnabled(true);QVERIFY(controller.running());pfservices::SettingsStore::flushPendingWrites();firstKey=QByteArray::fromStdString(store.load(error).mcpToken);QCOMPARE(firstKey.size(),64);
            controller.rotateKey();pfservices::SettingsStore::flushPendingWrites();const auto secondKey=QByteArray::fromStdString(store.load(error).mcpToken);QVERIFY(firstKey!=secondKey);
            QVERIFY(exchange(port,request(port,firstKey,QJsonDocument(message("ping")).toJson())).startsWith("HTTP/1.1 401"));
            QVERIFY(exchange(port,request(port,secondKey,QJsonDocument(message("ping")).toJson())).startsWith("HTTP/1.1 200"));}
        {pfui::McpController restarted(pfui::AnalysisController::instance(),path);QVERIFY(restarted.enabled());QVERIFY(restarted.running());restarted.setEnabled(false);QVERIFY(!restarted.running());pfservices::SettingsStore::flushPendingWrites();}
        const auto final=store.load(error);QCOMPARE(final.language,std::string("ru"));QCOMPARE(final.cachePath,std::string("D:/preserve-cache"));QVERIFY(!final.mcpEnabled);
    }
    void realVideoControlAndExport(){
        const auto source=qEnvironmentVariable("PF_REAL_MCP_SOURCE");
        if(source.isEmpty())QSKIP("Opt-in real-video MCP control/export integration");
        QTemporaryDir dir;
        auto* analysis=pfui::AnalysisController::instance();
        const auto previousCache=analysis->cachePath();
        const auto restore=qScopeGuard([&]{analysis->setCachePath(previousCache);pfservices::SettingsStore::flushPendingWrites();});
        const auto cacheRoot=qEnvironmentVariable("PF_REAL_MCP_CACHE",dir.filePath("cache"));
        analysis->setCachePath(cacheRoot);
        pfui::AppInfo::instance()->initializeBackendsAsync();
        QTRY_VERIFY_WITH_TIMEOUT(!pfui::AppInfo::instance()->backendInitializing(),30000);
        analysis->setModelPath(qEnvironmentVariable("PF_MODEL_PATH"));
        analysis->setProviderChoice("cuda");analysis->setQualityProfile("fast");
        analysis->setAnalysisMode("combined");analysis->setAccuracyPreset("fast");analysis->setExpandedSearch(false);
        pfui::McpController controller(analysis,dir.filePath("control.json"));controller.setAllowControl(true);
        const auto invoke=[&](const QString& name,const QJsonObject& args=QJsonObject{}){
            return controller.dispatch(message("tools/call",{{"name",name},{"arguments",args}}))["result"].toObject();
        };
        QVERIFY(!invoke("pf_apply_accuracy_preset",{{"preset","fast"}})["isError"].toBool());
        analysis->setSceneThreshold(27);analysis->setNormalizeSize(true);analysis->setMirrorPoses(true);
        QJsonArray sourcePaths{source};
        const auto second=qEnvironmentVariable("PF_REAL_MCP_SECOND_SOURCE");
        if (!second.isEmpty()) sourcePaths.append(second);
        analysis->setExpandedSearch(true);
        auto started=invoke("pf_start_analysis",{{"paths",sourcePaths}});
        QVERIFY2(!started["isError"].toBool(),qPrintable(QString::fromUtf8(QJsonDocument(started).toJson())));
        QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy(),900000);
        QVERIFY2(analysis->analysisCompleted(),qPrintable(analysis->status()));QVERIFY(!analysis->results().isEmpty());
        if(const auto report=qEnvironmentVariable("PF_REAL_MCP_REPORT");!report.isEmpty()) {
            QFile output(report);QVERIFY(output.open(QIODevice::WriteOnly));
            output.write(QJsonDocument::fromVariant(analysis->results()).toJson());
        }
        const auto row=analysis->results().first().toMap();const int id=row["id"].toInt();
        const auto ids=QJsonArray{id};
        const auto metadata=invoke("pf_get_video_info",{{"path",source}});
        QVERIFY(!metadata["isError"].toBool());
        QVERIFY(metadata["structuredContent"].toObject()["durationSeconds"].toDouble()>0);
        for(const auto& side:{QString("left"),QString("right")}){
            const auto preview=invoke("pf_get_preview",{{"id",id},{"side",side},{"width",320}});
            QVERIFY2(!preview["isError"].toBool(),qPrintable(QString::fromUtf8(QJsonDocument(preview).toJson())));
            bool hasImage=false;
            for(const auto& item:preview["content"].toArray())
                if(item.toObject()["type"].toString()=="image"){
                    hasImage=true;
                    QVERIFY(QByteArray::fromBase64(item.toObject()["data"].toString().toLatin1()).startsWith("\x89PNG"));
                }
            QVERIFY(hasImage);
        }
        const auto resultSet=invoke("pf_get_status")["structuredContent"].toObject()["resultSet"].toString();
        QVERIFY(!invoke("pf_set_visibility",{{"ids",ids},{"hidden",true},{"resultSet",resultSet}})["isError"].toBool());
        QVERIFY(analysis->results()[id].toMap()["hidden"].toBool());
        QVERIFY(!invoke("pf_undo_result_edit",{{"resultSet",resultSet}})["isError"].toBool());
        QVERIFY(!analysis->results()[id].toMap()["hidden"].toBool());
        QVERIFY(!invoke("pf_set_classification",{{"ids",ids},{"classification","wave"}})["isError"].toBool());
        QCOMPARE(analysis->results()[id].toMap()["classification"].toString(),QString("wave"));
        QVERIFY(!invoke("pf_set_review",{{"ids",ids},{"field","reviewed"},{"value",true}})["isError"].toBool());
        QVERIFY(analysis->results()[id].toMap()["reviewed"].toBool());
        const auto beforeGroup=analysis->results();
        QVERIFY(!invoke("pf_set_group",{{"ids",ids},{"group","Моя группа"}})["isError"].toBool());
        QCOMPARE(analysis->results()[id].toMap()["manualGroup"].toString(),QString("Моя группа"));
        QCOMPARE(analysis->results()[id].toMap()["classification"].toString(),QString("wave"));
        QVERIFY(!invoke("pf_undo_result_edit")["isError"].toBool());
        QCOMPARE(analysis->results(),beforeGroup);
        QVERIFY(!invoke("pf_remove_side",{{"id",id},{"side","left"}})["isError"].toBool());
        QVERIFY(analysis->results()[id].toMap()["leftRemoved"].toBool());
        QVERIFY(invoke("pf_remove_side",{{"id",id},{"side","right"}})["isError"].toBool());
        QVERIFY(!invoke("pf_undo_result_edit")["isError"].toBool());
        QCOMPARE(analysis->results(),beforeGroup);
        QVERIFY(!analysis->replaceResultSide(id,"left",id,"right"));
        QVERIFY(!analysis->replaceResultSide(id,"invented",id+1,"right"));
        QVERIFY(analysis->results().size()>1);
        const auto beforeComposition=analysis->results();
        int donorId=-1; QString donorSide;
        const auto partner=beforeComposition[id].toMap();
        for(const auto& candidate:beforeComposition) {
            const auto record=candidate.toMap(); if(record["id"].toInt()==id) continue;
            for(const auto& side:{QString("left"),QString("right")}) {
                if(record[side+"Source"]==partner["rightSource"]
                    && record[side+"ClipStart"]==partner["rightClipStart"]
                    && record[side+"ClipEnd"]==partner["rightClipEnd"]) continue;
                donorId=record["id"].toInt();donorSide=side;break;
            }
            if(donorId>=0) break;
        }
        QVERIFY(donorId>=0);
        const auto donor=beforeComposition[donorId].toMap();
        QVERIFY(analysis->replaceResultSide(id,"left",donorId,donorSide));
        QTRY_VERIFY_WITH_TIMEOUT(!analysis->reviewBusy(),30000);
        const auto composed=analysis->results()[id].toMap();
        QCOMPARE(composed["leftSource"],donor[donorSide+"Source"]);
        QCOMPARE(composed["leftClipStart"],donor[donorSide+"ClipStart"]);
        QCOMPARE(composed["leftClipEnd"],donor[donorSide+"ClipEnd"]);
        QCOMPARE(composed["rightSource"],beforeComposition[id].toMap()["rightSource"]);
        QCOMPARE(composed["rightClipStart"],beforeComposition[id].toMap()["rightClipStart"]);
        QCOMPARE(analysis->results()[donorId],beforeComposition[donorId]);
        QVERIFY(composed["manualPair"].toBool());
        QCOMPARE(composed["similarity"].toDouble(),0.0);
        QVERIFY(!composed["faceVerified"].toBool());
        QCOMPARE(composed["classification"].toString(),QString("wave"));
        QVERIFY(QFileInfo::exists(QUrl(composed["leftPreview"].toString()).toLocalFile()));
        QCOMPARE(analysis->undoResultEdit(),QVariantList{id});
        QCOMPARE(analysis->results(),beforeComposition);
        QVERIFY(analysis->removeResultSide(id,"left"));
        QVERIFY(analysis->removeResultSide(donorId,donorSide=="left"?"right":"left"));
        const auto singles=analysis->results();
        analysis->setResultReview({id},"hidden",true);
        QVERIFY(invoke("pf_combine_singles",{{"targetId",id},{"donorId",donorId}})["isError"].toBool());
        QVERIFY(!analysis->results()[donorId].toMap()["hidden"].toBool());
        QCOMPARE(analysis->undoResultEdit().size(),1);
        QCOMPARE(analysis->results(),singles);
        QVERIFY(!invoke("pf_combine_singles",{{"targetId",id},{"donorId",donorId}})["isError"].toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!analysis->reviewBusy(),30000);
        QVERIFY(!analysis->results()[id].toMap()["leftRemoved"].toBool());
        QVERIFY(!analysis->results()[id].toMap()["rightRemoved"].toBool());
        QVERIFY(analysis->results()[donorId].toMap()["hidden"].toBool());
        QCOMPARE(analysis->undoResultEdit().size(),2);
        QCOMPARE(analysis->results(),singles);
        QCOMPARE(analysis->undoResultEdit().size(),1);
        QCOMPARE(analysis->undoResultEdit().size(),1);
        QCOMPARE(analysis->results(),beforeComposition);
        QDir().mkpath(dir.filePath("export"));
        const QJsonObject exportArgs{{"ids",ids},{"folder",dir.filePath("export")},{"prefix","mcp-review"},{"mode","lossless"}};
        const auto files=[&]{QStringList paths;QDirIterator it(dir.filePath("export"),{"*.mov"},QDir::Files,QDirIterator::Subdirectories);while(it.hasNext())paths.append(it.next());return paths;};
        for(int pass=0;pass<2;++pass){
            const auto exported=invoke("pf_export_results",exportArgs);
            QVERIFY2(!exported["isError"].toBool(),qPrintable(QString::fromUtf8(QJsonDocument(exported).toJson())));
            QTRY_VERIFY_WITH_TIMEOUT(!analysis->exportBusy(),60000);
            const auto state=invoke("pf_get_status")["structuredContent"].toObject();
            QVERIFY2(state["exportSuccess"].toBool(),qPrintable(state["exportMessage"].toString()));
            QCOMPARE(files().size(),(pass+1)*2);
        }
        QDir().mkpath(dir.filePath("single"));
        QVERIFY(analysis->removeResultSide(id,"left"));
        auto singleArgs=exportArgs;singleArgs["folder"]=dir.filePath("single");
        QVERIFY(!invoke("pf_export_results",singleArgs)["isError"].toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!analysis->exportBusy(),60000);
        QVERIFY(invoke("pf_get_export_status")["structuredContent"].toObject()["success"].toBool());
        QDirIterator singleFiles(dir.filePath("single"),{"*.mov"},QDir::Files,QDirIterator::Subdirectories);
        int singleCount=0;while(singleFiles.hasNext()){singleFiles.next();++singleCount;}QCOMPARE(singleCount,1);
        QCOMPARE(analysis->undoResultEdit().size(),1);
        QVERIFY(analysis->replaceResultSide(id,"left",donorId,donorSide));
        QTRY_VERIFY_WITH_TIMEOUT(!analysis->reviewBusy(),30000);
        const auto ownedComposition=analysis->results()[id].toMap();
        const double a=ownedComposition["leftClipStart"].toDouble(), b=ownedComposition["leftClipEnd"].toDouble();
        const double c=ownedComposition["rightClipStart"].toDouble(), d=ownedComposition["rightClipEnd"].toDouble();
        const double offset=std::min(.01,(b-a)/4);
        for(int edit=0;edit<20;++edit) {
            QVERIFY(analysis->setResultRange(id,a+(edit%2?0:offset),b,c,d));
            QTRY_VERIFY_WITH_TIMEOUT(!analysis->reviewBusy(),30000);
        }
        QVERIFY(analysis->resetResultRange(id));
        const auto reset=analysis->results()[id].toMap();
        QCOMPARE(reset["leftClipStart"],ownedComposition["leftClipStart"]);
        QVERIFY(reset["manualPair"].toBool());QCOMPARE(reset["similarity"].toDouble(),0.0);
        QVERIFY(QFileInfo::exists(QUrl(reset["leftPreview"].toString()).toLocalFile()));
        QVERIFY(QFileInfo::exists(QUrl(reset["rightPreview"].toString()).toLocalFile()));
        const auto cacheContents = [&] {
            QMap<QString, QByteArray> values;
            QDirIterator it(cacheRoot, {"*.pfc"}, QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const auto path = it.next(); QFile file(path);
                if (!file.open(QIODevice::ReadOnly)) return QMap<QString, QByteArray>{};
                auto bytes = file.readAll();
                if (bytes.size() < 24) return QMap<QString, QByteArray>{};
                const auto keySize = qFromLittleEndian<quint32>(bytes.constData() + 12);
                if (keySize > 1024 || bytes.size() < 24 + keySize) return QMap<QString, QByteArray>{};
                const auto cacheKey = QString::fromUtf8(bytes.mid(24, keySize));
                if (cacheKey.startsWith("motion-v")) {
                    std::vector<pfcore::MotionWindow> windows; int scenes = 0;
                    const auto offset = 24 + keySize;
                    const auto payload = std::span(reinterpret_cast<const std::uint8_t*>(bytes.constData() + offset),
                        static_cast<std::size_t>(bytes.size() - offset));
                    if (!pfservices::deserializeMotionWindows(payload, windows, scenes)) return QMap<QString, QByteArray>{};
                    // Warm reuse may fill missing spatial views, while the
                    // expensive poses, identities and source times must stay exact.
                    for (auto& window : windows) { window.sceneView.clear(); window.sceneViewSampled = false; }
                    const auto observations = pfservices::serializeMotionWindows(windows, scenes);
                    bytes = QByteArray(reinterpret_cast<const char*>(observations.data()), observations.size());
                } else bytes = bytes.mid(24 + keySize);
                values.insert(cacheKey, bytes);
            }
            return values;
        };
        const auto originalCache = cacheContents(); QVERIFY(!originalCache.isEmpty());
        analysis->setExpandedSearch(true);
        const auto repeated = invoke("pf_start_analysis", {{"paths", sourcePaths}});
        QVERIFY(!repeated["isError"].toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!analysis->busy(), 300000);
        QVERIFY(analysis->analysisCompleted()); QVERIFY(!analysis->results().isEmpty());
        QCOMPARE(analysis->analyzedFrames(), 0LL); // no repeated pose recognition
        const auto reusedCache = cacheContents();
        // Expanded matching may add preview entries for newly admitted pairs.
        // Every pre-existing pose/identity observation and preview stays exact.
        for (auto it = originalCache.cbegin(); it != originalCache.cend(); ++it) {
            QVERIFY(reusedCache.contains(it.key()));
            const auto oldJson = QJsonDocument::fromJson(it.value());
            if (oldJson.isObject()) {
                // Expanded matching can append face-evidence observations.
                // It must keep every previously measured sample unchanged.
                const auto newJson = QJsonDocument::fromJson(reusedCache.value(it.key()));
                QVERIFY(newJson.isObject());
                const auto oldNotes = oldJson.object(), newNotes = newJson.object();
                for (auto note = oldNotes.begin(); note != oldNotes.end(); ++note) {
                    QVERIFY(newNotes.contains(note.key()));
                    QCOMPARE(newNotes.value(note.key()), note.value());
                }
            } else {
                QVERIFY2(reusedCache.value(it.key()) == it.value(), qPrintable(it.key()));
            }
        }
        for (const auto& result : analysis->results())
            QVERIFY(!result.toMap()["classificationManual"].toBool());
    }
    void officialSdkConnectsToApplication(){
        const auto python=qEnvironmentVariable("PF_TEST_MCP_PYTHON");if(python.isEmpty())QSKIP("Set PF_TEST_MCP_PYTHON to run the official SDK client");
        QTemporaryDir dir;pfui::McpController controller(pfui::AnalysisController::instance(),dir.filePath("settings.json"));QTcpServer unused;QVERIFY(unused.listen(QHostAddress::LocalHost,0));const auto port=unused.serverPort();unused.close();controller.setPort(port);controller.setEnabled(true);QVERIFY(controller.running());pfservices::SettingsStore::flushPendingWrites();std::string error;const auto settings=pfservices::SettingsStore(dir.filePath("settings.json").toStdString()).load(error);
        QProcess client;auto environment=QProcessEnvironment::systemEnvironment();environment.insert("PF_MCP_TEST_TOKEN",QString::fromStdString(settings.mcpToken));client.setProcessEnvironment(environment);
        client.start(python,{QStringLiteral(PF_MCP_CLIENT_SCRIPT),controller.endpoint()});QVERIFY(client.waitForStarted());QTRY_VERIFY_WITH_TIMEOUT(client.state()==QProcess::NotRunning,20000);
        QCOMPARE(client.exitCode(),0);QVERIFY(client.readAllStandardOutput().contains("MCP SDK integration passed"));
        controller.setEnabled(false);pfservices::SettingsStore::flushPendingWrites();
    }
};
QTEST_MAIN(McpTests)
#include "test_mcp.moc"
