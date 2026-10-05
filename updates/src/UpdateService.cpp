#include <pfupdate/UpdateService.hpp>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkRequest>
#include <QProcess>
#include <QTimer>
#include <QUuid>
#include <QThread>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace pfupdate {
UpdateService::UpdateService(UpdateOptions options,QObject* parent):QObject(parent),options_(std::move(options)),network_(this) {
    QDir().mkpath(options_.storageDirectory);
    QFile file(options_.storageDirectory+"/preferences.json");
    if(file.open(QIODevice::ReadOnly) && file.size()<64*1024) {
        const auto o=QJsonDocument::fromJson(file.readAll()).object();
        if(o["channel"]=="beta")channel_="beta";
        automaticCheck_=o["automaticCheck"].toBool(true);automaticDownload_=o["automaticDownload"].toBool(false);skipped_=o["skipped"].toString();
    }
    QFile result(options_.storageDirectory+"/last-result.json");
    if(result.open(QIODevice::ReadOnly) && result.size()<64*1024) {
        const auto o=QJsonDocument::fromJson(result.readAll()).object();
        if(o.contains("success") && !o["success"].toBool()) {state_="error";error_=o["error"].toString();visible_=true;}
        result.close();QFile::remove(result.fileName());
    }
}
UpdateService::~UpdateService(){if(reply_){reply_->disconnect(this);reply_->abort();}}
void UpdateService::savePreferences() {
    QSaveFile file(options_.storageDirectory+"/preferences.json");
    const auto bytes=QJsonDocument(QJsonObject{{"channel",channel_},{"automaticCheck",automaticCheck_},{"automaticDownload",automaticDownload_},{"skipped",skipped_}}).toJson();
    if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit())transition("error","Cannot save update preferences");
}
void UpdateService::setChannel(const QString& value){if(busy() || (value!="stable" && value!="beta") || value==channel_)return;channel_=value;skipped_.clear();manifest_.reset();state_="idle";savePreferences();emit changed();}
void UpdateService::setAutomaticCheck(bool value){automaticCheck_=value;savePreferences();emit changed();}
void UpdateService::setAutomaticDownload(bool value){automaticDownload_=value;savePreferences();emit changed();}
void UpdateService::transition(const QString& state,const QString& error){state_=state;error_=error;emit changed();}
void UpdateService::startup(){if(automaticCheck_ && state_!="error")QTimer::singleShot(2500,this,[this]{if(automaticCheck_)check(false);});}
bool UpdateService::allowedUrl(const QUrl& url) const {
    if(!url.userInfo().isEmpty() || !url.fragment().isEmpty())return false;
    if(options_.allowTestHttp && url.scheme()=="http" && (url.host()=="127.0.0.1" || url.host()=="localhost"))return true;
    if(url.scheme()!="https" || (url.port()!=-1 && url.port()!=443))return false;
    return url.host()=="api.github.com" || url.host()=="github.com" || url.host()=="release-assets.githubusercontent.com" || url.host()=="objects.githubusercontent.com";
}
void UpdateService::fetch(const QUrl& url,qsizetype limit,std::function<void(QByteArray)> done) {
    if(!allowedUrl(url)){transition("error","Untrusted update URL");return;}
    QNetworkRequest request(url);request.setRawHeader("User-Agent","ParallelFinder-Updater/1");request.setRawHeader("Accept","application/vnd.github+json");request.setTransferTimeout(20000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::UserVerifiedRedirectPolicy);
    auto* reply=network_.get(request);reply_=reply;const auto serial=serial_;auto bytes=std::make_shared<QByteArray>();
    connect(reply,&QNetworkReply::redirected,this,[this,reply](const QUrl& target){if(allowedUrl(target))reply->redirectAllowed();else reply->abort();});
    connect(reply,&QNetworkReply::readyRead,this,[reply,bytes,limit]{bytes->append(reply->readAll());if(bytes->size()>limit)reply->abort();});
    connect(reply,&QNetworkReply::finished,this,[this,reply,bytes,limit,serial,done=std::move(done)] {
        bytes->append(reply->readAll());reply->deleteLater();if(serial!=serial_)return;
        reply_=nullptr;
        if(reply->error()!=QNetworkReply::NoError || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()!=200 || bytes->size()>limit){transition("error","Network request failed: "+reply->errorString());return;}
        done(*bytes);
    });
}
void UpdateService::check(bool manual) {
    if(busy())return;
    if(!packagePath_.isEmpty()){QFile::remove(packagePath_);packagePath_.clear();}
    manual_=manual;visible_=manual;manifest_.reset();received_=0;speed_=0;releases_=QJsonArray{};++serial_;transition("checking");fetchPage(1);
}
void UpdateService::fetchPage(int page) {
    auto url=options_.releasesUrl;url.setQuery(QString("per_page=100&page=%1").arg(page));
    fetch(url,8*1024*1024,[this,page](const QByteArray& bytes) {
        QJsonParseError error;const auto doc=QJsonDocument::fromJson(bytes,&error);
        if(error.error!=QJsonParseError::NoError || !doc.isArray()){transition("error","Invalid release feed");return;}
        for(const auto& row:doc.array())releases_.append(row);
        if(doc.array().size()==100 && page<5)fetchPage(page+1);else chooseRelease();
    });
}
void UpdateService::chooseRelease() {
    QJsonObject best;
    for(const auto& row:releases_) {
        const auto o=row.toObject();const auto version=o["tag_name"].toString();const auto newer=compareVersions(version,currentVersion());
        if(o["draft"].toBool() || !newer || *newer<=0 || (channel_=="stable" && (o["prerelease"].toBool() || version.contains('-'))))continue;
        if(best.isEmpty() || compareVersions(version,best["tag_name"].toString()).value_or(-1)>0)best=o;
    }
    if(best.isEmpty()){transition("upToDate");return;}
    if(!manual_ && skipped_==best["tag_name"].toString()){transition("idle");return;}
    QMap<QString,QUrl> assets;QMap<QString,qint64> sizes;
    for(const auto& a:best["assets"].toArray()) {const auto o=a.toObject();assets[o["name"].toString()]=QUrl(o["browser_download_url"].toString());sizes[o["name"].toString()]=o["size"].toInteger();}
    if(!assets.contains("update.json") || !assets.contains("update.json.sig")){transition("error","New release has no signed update package");return;}
    const auto version=best["tag_name"].toString();
    fetch(assets["update.json"],2*1024*1024,[this,assets,sizes,version](const QByteArray& bytes) {
        manifestBytes_=bytes;
        fetch(assets["update.json.sig"],64,[this,assets,sizes,version](const QByteArray& signature) {
            signature_=signature;QString error;manifest_=verifyManifest(manifestBytes_,signature_,options_.publicKey,error);
            if(!manifest_){transition("error",error);return;}
            if(manifest_->version!=version || (channel_=="stable" && manifest_->channel!="stable") || !assets.contains(manifest_->asset) || sizes[manifest_->asset]!=manifest_->size){manifest_.reset();transition("error","Signed metadata does not match release");return;}
            assetUrl_=assets[manifest_->asset];visible_=true;transition("available");if(automaticDownload_)download();
        });
    });
}
void UpdateService::download() {
    if(!manifest_ || (state_!="available" && state_!="cancelled" && state_!="error"))return;
    if(!allowedUrl(assetUrl_)){transition("error","Untrusted package URL");return;}
    ++serial_;const auto serial=serial_;received_=0;speed_=0;hash_.reset();
    packagePath_=options_.storageDirectory+"/update-"+QUuid::createUuid().toString(QUuid::WithoutBraces)+".zip";
    downloadFile_=std::make_unique<QSaveFile>(packagePath_);if(!downloadFile_->open(QIODevice::WriteOnly)){transition("error","Cannot create update download");return;}
    QNetworkRequest request(assetUrl_);request.setTransferTimeout(30000);request.setRawHeader("User-Agent","ParallelFinder-Updater/1");request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,QNetworkRequest::UserVerifiedRedirectPolicy);
    auto* reply=network_.get(request);reply_=reply;transferTimer_.start();uiTimer_.start();transition("downloading");
    connect(reply,&QNetworkReply::redirected,this,[this,reply](const QUrl& target){if(allowedUrl(target))reply->redirectAllowed();else reply->abort();});
    const auto consume=[this,reply,serial] {
        if(serial!=serial_ || !downloadFile_)return;
        const auto bytes=reply->readAll();received_+=bytes.size();
        if(received_>totalBytes() || downloadFile_->write(bytes)!=bytes.size()){reply->abort();return;}
        hash_.addData(bytes);speed_=received_*1000.0/std::max<qint64>(1,transferTimer_.elapsed());
        if(uiTimer_.elapsed()>=100){uiTimer_.restart();emit changed();}
    };
    connect(reply,&QNetworkReply::readyRead,this,consume);
    connect(reply,&QNetworkReply::finished,this,[this,reply,serial,consume] {
        reply->deleteLater();if(serial!=serial_)return;consume();reply_=nullptr;
        if(reply->error()!=QNetworkReply::NoError || reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()!=200){downloadFile_.reset();transition("error","Download failed: "+reply->errorString());return;}
        transition("verifying");QString error;
        if(!verifyManifest(manifestBytes_,signature_,options_.publicKey,error) || received_!=totalBytes() || hash_.result()!=manifest_->sha256 || !downloadFile_->commit()) {
            downloadFile_.reset();transition("error",error.isEmpty()?"Update integrity check failed":error);return;
        }
        downloadFile_.reset();transition("ready");
    });
}
void UpdateService::cancel() {
    if(state_=="installing")return;
    ++serial_;
    if(reply_){reply_->abort();reply_=nullptr;}downloadFile_.reset();
    if(!packagePath_.isEmpty()){QFile::remove(packagePath_);packagePath_.clear();}
    received_=0;speed_=0;transition("cancelled");
}
void UpdateService::later(){visible_=false;emit changed();}
void UpdateService::skip(){if(busy())return;if(manifest_)skipped_=manifest_->version;savePreferences();visible_=false;emit changed();}
bool UpdateService::launch(const QString& request,QString& error) {
    const auto runtime=options_.storageDirectory+"/helper-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
    if(!QDir().mkpath(runtime)){error="Cannot prepare updater runtime";return false;}
    const QDir installation(options_.installationDirectory);
    for(const auto& name:installation.entryList({"*.dll","ParallelFinderUpdater.exe"},QDir::Files))
        if(!QFile::copy(installation.filePath(name),runtime+'/'+name)){error="Cannot copy updater runtime";return false;}
    QProcess process;process.setProgram(runtime+"/ParallelFinderUpdater.exe");process.setArguments({"--request",request});process.setWorkingDirectory(runtime);
#ifdef Q_OS_WIN
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args){args->startupInfo->dwFlags|=STARTF_USESHOWWINDOW;args->startupInfo->wShowWindow=SW_HIDE;});
#endif
    if(!process.startDetached()){error="Cannot launch updater";return false;}return true;
}
void UpdateService::install() {
    if(state_!="ready" || !manifest_)return;
    if(!canInstall_()){transition("ready","Finish analysis and export before installing");return;}
    const auto requestPath=options_.storageDirectory+"/request-"+QUuid::createUuid().toString(QUuid::WithoutBraces)+".json";
    QSaveFile file(requestPath);const auto bytes=QJsonDocument(QJsonObject{{"schema",1},{"root",options_.installationDirectory},{"package",packagePath_},{"manifest",QString::fromLatin1(manifestBytes_.toBase64())},{"signature",QString::fromLatin1(signature_.toBase64())},{"currentVersion",currentVersion()},{"parentPid",QCoreApplication::applicationPid()},{"consent",true}}).toJson();
    if(!file.open(QIODevice::WriteOnly) || file.write(bytes)!=bytes.size() || !file.commit()){transition("error","Cannot persist updater request");return;}
    QString error;const bool launched=options_.launchUpdater?options_.launchUpdater(requestPath,error):launch(requestPath,error);
    if(!launched){transition("error",error);return;}
    transition("installing");
    if(options_.launchUpdater){emit quitRequested();return;}
    // Detached process creation alone does not prove its DLLs loaded. Keep
    // this application alive until the verified updater acknowledges handoff.
    auto* timer=new QTimer(this);timer->setInterval(100);auto elapsed=std::make_shared<QElapsedTimer>();elapsed->start();
    connect(timer,&QTimer::timeout,this,[this,timer,elapsed,requestPath] {
        QFile ready(requestPath+".ready");
        if(ready.open(QIODevice::ReadOnly) && QString::fromUtf8(ready.read(256))==newVersion()) {
            ready.close();QFile::remove(ready.fileName());timer->stop();timer->deleteLater();emit quitRequested();return;
        }
        if(elapsed->elapsed()>30000) {
            QSaveFile cancel(requestPath+".cancel");if(cancel.open(QIODevice::WriteOnly)){cancel.write("cancel");cancel.commit();}
            timer->stop();timer->deleteLater();transition("error","Updater did not acknowledge startup; application kept open");
        }
    });
    timer->start();
}
}
