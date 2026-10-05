#pragma once
#include <pfupdate/UpdateManifest.hpp>
#include <QObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QNetworkReply>
#include <QSaveFile>
#include <QElapsedTimer>
#include <QUrl>
#include <QJsonArray>
#include <QCryptographicHash>
#include <functional>

namespace pfupdate {
struct UpdateOptions {
    QString currentVersion, storageDirectory, installationDirectory;
    QUrl releasesUrl{"https://api.github.com/repos/Pozit1vchic/Parallel-Finder/releases"};
    QByteArray publicKey=trustedPublicKey();
    bool allowTestHttp=false;
    std::function<bool(const QString& request,QString& error)> launchUpdater;
};
class UpdateService final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString newVersion READ newVersion NOTIFY changed)
    Q_PROPERTY(QString changelog READ changelog NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(qint64 totalBytes READ totalBytes NOTIFY changed)
    Q_PROPERTY(qint64 receivedBytes READ receivedBytes NOTIFY changed)
    Q_PROPERTY(double bytesPerSecond READ bytesPerSecond NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(bool dialogVisible READ dialogVisible NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString channel READ channel WRITE setChannel NOTIFY changed)
    Q_PROPERTY(bool automaticCheck READ automaticCheck WRITE setAutomaticCheck NOTIFY changed)
    Q_PROPERTY(bool automaticDownload READ automaticDownload WRITE setAutomaticDownload NOTIFY changed)
public:
    explicit UpdateService(UpdateOptions options,QObject* parent=nullptr);
    ~UpdateService() override;
    QString state() const{return state_;}
    QString currentVersion() const{return options_.currentVersion;}
    QString newVersion() const{return manifest_?manifest_->version:QString{};}
    QString changelog() const{return manifest_?manifest_->changelog:QString{};}
    QString error() const{return error_;}
    qint64 totalBytes() const{return manifest_?manifest_->size:0;}
    qint64 receivedBytes() const{return received_;}
    double bytesPerSecond() const{return speed_;}
    double progress() const{return totalBytes()>0?std::min(1.0,double(received_)/totalBytes()):0;}
    bool dialogVisible() const{return visible_;}
    bool busy() const{return state_=="checking" || state_=="downloading" || state_=="verifying" || state_=="installing";}
    QString channel() const{return channel_;}
    bool automaticCheck() const{return automaticCheck_;}
    bool automaticDownload() const{return automaticDownload_;}
    void setChannel(const QString& value);
    void setAutomaticCheck(bool value);
    void setAutomaticDownload(bool value);
    void startup();
    Q_INVOKABLE void check(bool manual=true);
    Q_INVOKABLE void download();
    Q_INVOKABLE void install();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void later();
    Q_INVOKABLE void skip();
    void setCanInstall(std::function<bool()> guard){canInstall_=std::move(guard);}
signals:
    void changed();
    void quitRequested();
private:
    UpdateOptions options_;
    QNetworkAccessManager network_;
    QPointer<QNetworkReply> reply_;
    std::unique_ptr<QSaveFile> downloadFile_;
    QCryptographicHash hash_{QCryptographicHash::Sha256};
    QElapsedTimer transferTimer_,uiTimer_;
    QString state_="idle",error_,channel_="stable",skipped_,packagePath_;
    bool visible_=false,automaticCheck_=true,automaticDownload_=false,manual_=false;
    qint64 received_=0;double speed_=0;quint64 serial_=0;
    std::optional<Manifest> manifest_;
    QByteArray manifestBytes_,signature_;
    QUrl assetUrl_;
    QJsonArray releases_;
    std::function<bool()> canInstall_=[] {return true;};
    void savePreferences();
    void transition(const QString& state,const QString& error={});
    bool allowedUrl(const QUrl& url) const;
    void fetch(const QUrl& url,qsizetype limit,std::function<void(QByteArray)> done);
    void fetchPage(int page);
    void chooseRelease();
    bool launch(const QString& request,QString& error);
};
}
