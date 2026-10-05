#include <pfupdate/UpdateService.hpp>
#include <pfupdate/UpdateTransaction.hpp>
#include <QTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QDir>
#include <QSaveFile>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickWindow>
#include <QQuickItem>
#include <QProcess>
#include <AppInfo.h>
#include <archive.h>
#include <archive_entry.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <memory>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
QByteArray digest(QByteArray bytes){return QCryptographicHash::hash(bytes,QCryptographicHash::Sha256);}
void write(const QString& path,const QByteArray& bytes){QDir().mkpath(QFileInfo(path).absolutePath());QFile f(path);if(f.open(QIODevice::WriteOnly))f.write(bytes);}
QByteArray read(const QString& path){QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray{};}
struct Fixture {
    QTemporaryDir directory;
    std::unique_ptr<EVP_PKEY,decltype(&EVP_PKEY_free)> key{nullptr,EVP_PKEY_free};
    QByteArray publicKey,bytes,signature,zip;
    QJsonObject object;
    pfupdate::Manifest manifest;
    Fixture() {
        EVP_PKEY_CTX* context=EVP_PKEY_CTX_new_id(EVP_PKEY_ED25519,nullptr);EVP_PKEY_keygen_init(context);EVP_PKEY* raw=nullptr;EVP_PKEY_keygen(context,&raw);EVP_PKEY_CTX_free(context);key.reset(raw);
        BIO* bio=BIO_new(BIO_s_mem());PEM_write_bio_PUBKEY(bio,key.get());char* data=nullptr;const auto size=BIO_get_mem_data(bio,&data);publicKey=QByteArray(data,size);BIO_free(bio);
        const auto zipPath=directory.path()+"/package.zip";
        archive* a=archive_write_new();archive_write_set_format_zip(a);archive_write_open_filename(a,zipPath.toUtf8().constData());
        QJsonArray files;
        for(const auto& item:QVector<QPair<QString,QByteArray>>{{"ParallelFinder.exe","new-application"},{"ParallelFinderUpdater.exe","new-updater"},{"qml/test.qml","new-qml"}}) {
            archive_entry* e=archive_entry_new();const auto path=("ParallelFinder/"+item.first).toUtf8();archive_entry_set_pathname(e,path.constData());archive_entry_set_size(e,item.second.size());archive_entry_set_filetype(e,AE_IFREG);archive_entry_set_perm(e,0644);archive_write_header(a,e);archive_write_data(a,item.second.constData(),item.second.size());archive_entry_free(e);
            files.append(QJsonObject{{"path",item.first},{"size",item.second.size()},{"sha256",QString::fromLatin1(digest(item.second).toHex())}});
        }
        archive_write_close(a);archive_write_free(a);zip=read(zipPath);
        object={{"schema",1},{"repository","Pozit1vchic/Parallel-Finder"},{"platform","windows-x64"},{"version","1.0.0"},{"channel","stable"},{"asset","ParallelFinder-1.0.0-Portable-x64.zip"},{"size",zip.size()},{"sha256",QString::fromLatin1(digest(zip).toHex())},{"changelog","Real change list"},{"files",files}};
        resign();
    }
    void resign() {
        bytes=QJsonDocument(object).toJson(QJsonDocument::Compact);signature.resize(64);std::size_t size=64;
        EVP_MD_CTX* ctx=EVP_MD_CTX_new();EVP_DigestSignInit(ctx,nullptr,nullptr,nullptr,key.get());EVP_DigestSign(ctx,reinterpret_cast<unsigned char*>(signature.data()),&size,reinterpret_cast<const unsigned char*>(bytes.constData()),bytes.size());EVP_MD_CTX_free(ctx);
        QString error;manifest=pfupdate::verifyManifest(bytes,signature,publicKey,error).value_or(pfupdate::Manifest{});
    }
    QString root(){const auto path=directory.path()+"/installation";write(path+"/ParallelFinder.exe","old-application");write(path+"/unins000.exe","existing-installer-uninstaller");write(path+"/user-file.txt","portable-user-file");return path;}
    QString package(){const auto path=directory.path()+"/download.zip";write(path,zip);return path;}
    void repackage(const QMap<QString,QByteArray>& contents) {
        const auto path=directory.path()+"/process.zip";archive* a=archive_write_new();archive_write_set_format_zip(a);archive_write_open_filename(a,path.toUtf8().constData());QJsonArray files;
        for(auto it=contents.begin();it!=contents.end();++it) {
            archive_entry* e=archive_entry_new();const auto name=("ParallelFinder/"+it.key()).toUtf8();archive_entry_set_pathname(e,name.constData());archive_entry_set_size(e,it.value().size());archive_entry_set_filetype(e,AE_IFREG);archive_entry_set_perm(e,0644);archive_write_header(a,e);archive_write_data(a,it.value().constData(),it.value().size());archive_entry_free(e);
            files.append(QJsonObject{{"path",it.key()},{"size",it.value().size()},{"sha256",QString::fromLatin1(digest(it.value()).toHex())}});
        }
        archive_write_close(a);archive_write_free(a);zip=read(path);object["files"]=files;object["size"]=zip.size();object["sha256"]=QString::fromLatin1(digest(zip).toHex());resign();
    }
};
class Server : public QTcpServer {
public:
    QMap<QByteArray,QByteArray> body;
    QSet<QByteArray> delayed;
    QSet<QByteArray> partial;
    Server() {
        listen(QHostAddress::LocalHost);
        connect(this,&QTcpServer::newConnection,this,[this] {
            auto* socket=nextPendingConnection();connect(socket,&QTcpSocket::disconnected,socket,&QObject::deleteLater);
            connect(socket,&QTcpSocket::readyRead,this,[this,socket] {
                auto request=socket->property("request").toByteArray()+socket->readAll();socket->setProperty("request",request);
                if(!request.contains("\r\n\r\n") || socket->property("responded").toBool())return;
                socket->setProperty("responded",true);const auto path=request.split(' ').value(1).split('?').first();
                if(delayed.contains(path))return;
                const bool found=body.contains(path);const auto bytes=body.value(path);
                socket->write("HTTP/1.1 "+QByteArray(found?"200 OK":"503 Unavailable")+"\r\nContent-Length: "+QByteArray::number(bytes.size())+"\r\nConnection: close\r\n\r\n"+(partial.contains(path)?bytes.left(bytes.size()/2):bytes));
                if(!partial.contains(path))socket->disconnectFromHost();
            });
        });
    }
    QUrl url(const QString& path)const{return QUrl(QString("http://127.0.0.1:%1%2").arg(serverPort()).arg(path));}
    void release(Fixture& f,const QString& version="1.0.0",bool prerelease=false) {
        QJsonArray assets;
        for(const auto& item:QVector<QPair<QString,QByteArray>>{{"update.json",f.bytes},{"update.json.sig",f.signature},{f.manifest.asset,f.zip}}) {
            body['/'+item.first.toUtf8()]=item.second;assets.append(QJsonObject{{"name",item.first},{"size",item.second.size()},{"browser_download_url",url('/'+item.first).toString()}});
        }
        body["/releases"]=QJsonDocument(QJsonArray{QJsonObject{{"tag_name",version},{"draft",false},{"prerelease",prerelease},{"assets",assets}}}).toJson();
    }
};
pfupdate::UpdateOptions options(Fixture& f,Server& server) {
    pfupdate::UpdateOptions o;o.currentVersion="0.1.0-rc.17";o.storageDirectory=f.directory.path()+"/cache";o.installationDirectory=f.root();o.publicKey=f.publicKey;o.releasesUrl=server.url("/releases");o.allowTestHttp=true;return o;
}
}
class UpdateTests:public QObject {
    Q_OBJECT
private slots:
    void versions();
    void signaturesAndPaths();
    void foundAndDownloaded();
    void noUpdates();
    void networkError();
    void damagedDownload();
    void cancellation();
    void skipAndChannels();
    void automaticDownloadRequiresConsent();
    void successfulInstallation();
    void installationErrorRollsBack();
    void restartErrorRollsBack();
    void recoveryAfterInterruptedActivation();
    void refusesUnknownStaging();
    void dialogMatchesThemeAndShowsDownload();
    void unsignedReleaseRefused();
    void signedInventoryTamperingRejected();
#ifdef Q_OS_WIN
    void updaterProcessRestartsAndRollsBack_data();
    void updaterProcessRestartsAndRollsBack();
    void productionUpdaterRejectsTestKey();
    void lockedDirectoryRollsBack();
#endif
};
void UpdateTests::versions() {
    QVERIFY(pfupdate::compareVersions("v1.0.0","1.0.0-rc.17").value()>0);
    QVERIFY(pfupdate::compareVersions("0.1.0-rc.17","0.1.0-rc.9").value()>0);
    QVERIFY(pfupdate::compareVersions("0.1.0-rc.16.4.1","0.1.0-rc.16.4").value()>0);
    QVERIFY(!pfupdate::compareVersions("runtime-v1","1.0.0"));
    QVERIFY(!pfupdate::compareVersions("1.0.0-rc.01","1.0.0"));
}
void UpdateTests::signaturesAndPaths() {
    Fixture f;QString error;QVERIFY(pfupdate::verifyManifest(f.bytes,f.signature,f.publicKey,error));
    auto altered=f.bytes;altered[10]^=1;QVERIFY(!pfupdate::verifyManifest(altered,f.signature,f.publicKey,error));
    Fixture stranger;QVERIFY(!pfupdate::verifyManifest(f.bytes,f.signature,stranger.publicKey,error));
    for(const auto& p:{"../evil","C:/evil","/evil","dir\\evil","NUL.txt","a/../../x","x:stream","a./x"})QVERIFY(!pfupdate::safeRelativePath(p));
    auto files=f.object["files"].toArray();auto item=files[0].toObject();item["path"]="../ParallelFinder.exe";files[0]=item;f.object["files"]=files;f.resign();QVERIFY(!pfupdate::verifyManifest(f.bytes,f.signature,f.publicKey,error));
}
void UpdateTests::foundAndDownloaded() {
    Fixture f;Server server;server.release(f);pfupdate::UpdateService service(options(f,server));QSignalSpy quit(&service,&pfupdate::UpdateService::quitRequested);
    service.check();QTRY_COMPARE(service.state(),QString("available"));QCOMPARE(service.newVersion(),QString("1.0.0"));QCOMPARE(service.changelog(),QString("Real change list"));
    service.download();QTRY_COMPARE(service.state(),QString("ready"));QCOMPARE(service.receivedBytes(),qint64(f.zip.size()));QCOMPARE(service.progress(),1.0);QCOMPARE(quit.size(),0);
}
void UpdateTests::noUpdates(){Fixture f;Server server;server.body["/releases"]="[]";pfupdate::UpdateService service(options(f,server));service.check();QTRY_COMPARE(service.state(),QString("upToDate"));}
void UpdateTests::networkError(){Fixture f;Server server;pfupdate::UpdateService service(options(f,server));service.check();QTRY_COMPARE(service.state(),QString("error"));QVERIFY(!service.error().isEmpty());}
void UpdateTests::damagedDownload(){Fixture f;Server server;server.release(f);auto& bytes=server.body['/'+f.manifest.asset.toUtf8()];bytes[bytes.size()/2]^=1;pfupdate::UpdateService service(options(f,server));service.check();QTRY_COMPARE(service.state(),QString("available"));service.download();QTRY_COMPARE(service.state(),QString("error"));QVERIFY(service.error().contains("integrity"));}
void UpdateTests::cancellation(){Fixture f;Server server;server.release(f);server.partial.insert('/'+f.manifest.asset.toUtf8());pfupdate::UpdateService service(options(f,server));service.check();QTRY_COMPARE(service.state(),QString("available"));service.download();QCOMPARE(service.state(),QString("downloading"));QTRY_VERIFY(service.receivedBytes()>0);QVERIFY(service.progress()>0 && service.progress()<1);QVERIFY(service.bytesPerSecond()>0);service.cancel();QTest::qWait(50);QCOMPARE(service.state(),QString("cancelled"));QVERIFY(QDir(f.directory.path()+"/cache").entryList({"*.zip"},QDir::Files).isEmpty());}
void UpdateTests::skipAndChannels(){Fixture f;f.object["version"]="1.0.0-beta.2";f.object["channel"]="beta";f.resign();Server server;server.release(f,"1.0.0-beta.2",true);pfupdate::UpdateService service(options(f,server));service.check();QTRY_COMPARE(service.state(),QString("upToDate"));service.setChannel("beta");service.check();QTRY_COMPARE(service.state(),QString("available"));service.skip();service.check(false);QTRY_COMPARE(service.state(),QString("idle"));QVERIFY(!service.dialogVisible());service.check();QTRY_COMPARE(service.state(),QString("available"));}
void UpdateTests::automaticDownloadRequiresConsent() {
    Fixture f;Server server;server.release(f);auto o=options(f,server);int installs=0;o.launchUpdater=[&](const QString& request,QString&){++installs;return read(request).contains("\"consent\": true");};
    pfupdate::UpdateService service(o);QSignalSpy quit(&service,&pfupdate::UpdateService::quitRequested);service.setAutomaticDownload(true);service.check(false);QTRY_COMPARE(service.state(),QString("ready"));QCOMPARE(installs,0);QCOMPARE(quit.size(),0);
    service.setCanInstall([]{return false;});service.install();QCOMPARE(installs,0);service.setCanInstall([]{return true;});service.install();QCOMPARE(installs,1);QCOMPARE(quit.size(),1);
}
void UpdateTests::successfulInstallation() {
    Fixture f;QString error;const auto root=f.root();pfupdate::UpdateTransaction transaction(root);int restarted=0;
    pfupdate::InstallHooks hooks;hooks.healthCheck=[](const QString& exe,QString&){return read(exe)=="new-application";};hooks.restart=[&](const QString& exe,QString&){++restarted;return read(exe)=="new-application";};
    QVERIFY2(transaction.install(f.package(),f.manifest,hooks,error),qPrintable(error));QCOMPARE(restarted,1);QCOMPARE(read(root+"/ParallelFinder.exe"),QByteArray("new-application"));QCOMPARE(read(root+"/unins000.exe"),QByteArray("existing-installer-uninstaller"));QCOMPARE(read(root+"/user-file.txt"),QByteArray("portable-user-file"));QVERIFY(!QFileInfo::exists(transaction.journalPath()));
}
void UpdateTests::installationErrorRollsBack() {
    Fixture f;QString error;const auto root=f.root();pfupdate::UpdateTransaction transaction(root);pfupdate::InstallHooks hooks;hooks.beforeActivate=[](QString& e){e="simulated permission error";return false;};
    QVERIFY(!transaction.install(f.package(),f.manifest,hooks,error));QCOMPARE(read(root+"/ParallelFinder.exe"),QByteArray("old-application"));QVERIFY(!QFileInfo::exists(root+".pf-stage"));
}
void UpdateTests::restartErrorRollsBack(){Fixture f;QString error;const auto root=f.root();pfupdate::UpdateTransaction transaction(root);pfupdate::InstallHooks hooks;hooks.restart=[](const QString&,QString& e){e="simulated restart failure";return false;};QVERIFY(!transaction.install(f.package(),f.manifest,hooks,error));QCOMPARE(read(root+"/ParallelFinder.exe"),QByteArray("old-application"));QVERIFY(!QFileInfo::exists(root+".pf-backup"));}
void UpdateTests::recoveryAfterInterruptedActivation(){Fixture f;QString error;const auto root=f.root();QVERIFY(QDir().rename(root,root+".pf-backup"));write(root+"/ParallelFinder.exe","half-activated");write(root+".pf-update.json",QJsonDocument(QJsonObject{{"schema",1},{"root",root},{"phase","activated"}}).toJson());pfupdate::UpdateTransaction transaction(root);QVERIFY(transaction.recover(error));QCOMPARE(read(root+"/ParallelFinder.exe"),QByteArray("old-application"));}
void UpdateTests::refusesUnknownStaging(){Fixture f;QString error;const auto root=f.root();write(root+".pf-stage/keep.txt","must not delete");pfupdate::UpdateTransaction transaction(root);QVERIFY(!transaction.install(f.package(),f.manifest,{},error));QCOMPARE(read(root+".pf-stage/keep.txt"),QByteArray("must not delete"));}
void UpdateTests::unsignedReleaseRefused(){Fixture f;Server server;server.release(f);server.body["/update.json.sig"]=QByteArray(64,'x');pfupdate::UpdateService service(options(f,server));service.check();QTRY_COMPARE(service.state(),QString("error"));QVERIFY(service.error().contains("signature"));QVERIFY(service.newVersion().isEmpty());}
void UpdateTests::signedInventoryTamperingRejected(){Fixture f;QString error;auto files=f.object["files"].toArray();auto item=files[0].toObject();item["sha256"]=QString(64,'0');files[0]=item;f.object["files"]=files;f.resign();const auto root=f.root();pfupdate::UpdateTransaction transaction(root);QVERIFY(!transaction.install(f.package(),f.manifest,{},error));QCOMPARE(read(root+"/ParallelFinder.exe"),QByteArray("old-application"));QVERIFY(error.contains("checksum"));}
void UpdateTests::dialogMatchesThemeAndShowsDownload() {
    Fixture f;QByteArray padding(256*1024,0);RAND_bytes(reinterpret_cast<unsigned char*>(padding.data()),padding.size());f.repackage({{"ParallelFinder.exe","test-app"},{"ParallelFinderUpdater.exe","test-updater"},{"data.bin",padding}});
    Server server;server.release(f);server.partial.insert('/'+f.manifest.asset.toUtf8());pfupdate::UpdateService service(options(f,server));
    pfui::AppInfo::registerQmlTypes();QQmlApplicationEngine engine;engine.addImportPath("qrc:/qt/qml");
    engine.load(QUrl("qrc:/qt/qml/PfUi/qml/Main.qml"));
    auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().value(0));QVERIFY(window);
    auto* popup=window->findChild<QObject*>("updateDialog");QVERIFY(popup);
    popup->setProperty("service",QVariant::fromValue<QObject*>(&service));
    service.check();QTRY_COMPARE(service.state(),QString("available"));QTRY_VERIFY(popup->property("opened").toBool());
    auto* button=popup->findChild<QObject*>("updatePrimaryButton");auto* progress=popup->findChild<QQuickItem*>("updateProgress");QVERIFY(button && progress);QVERIFY(button->property("primary").toBool());QCOMPARE(popup->property("width").toInt(),560);
    service.download();QTRY_VERIFY(progress->isVisible());QTRY_VERIFY(service.receivedBytes()>0);QCOMPARE(button->property("text").toString(),QString("Cancel download"));
    const auto capture=qEnvironmentVariable("PF_UI_CAPTURE_DIR");if(!capture.isEmpty()){QDir().mkpath(capture);QTest::qWait(200);QVERIFY(window->grabWindow().save(capture+"/update-download.png"));}
    service.cancel();service.check();QTRY_COMPARE(service.state(),QString("available"));
    if(!capture.isEmpty()){QTest::qWait(200);QVERIFY(window->grabWindow().save(capture+"/update-available.png"));}
    service.later();QTRY_VERIFY(!popup->property("opened").toBool());
}
#ifdef Q_OS_WIN
void UpdateTests::updaterProcessRestartsAndRollsBack_data() {
    QTest::addColumn<bool>("failHealth");QTest::addColumn<bool>("cancelHandoff");
    QTest::newRow("successful-restart")<<false<<false;QTest::newRow("failed-health-restores-old")<<true<<false;
    QTest::newRow("cancelled-handoff-keeps-old")<<false<<true;
}
void UpdateTests::updaterProcessRestartsAndRollsBack() {
    QFETCH(bool,failHealth);QFETCH(bool,cancelHandoff);Fixture f;const auto stub=read(PF_UPDATE_APP_STUB);QVERIFY(!stub.isEmpty());
    QMap<QString,QByteArray> contents{{"ParallelFinder.exe",stub},{"ParallelFinderUpdater.exe",read(PF_UPDATE_TEST_WORKER)},{"version.txt","1.0.0"}};
    if(failHealth)contents["fail-health.txt"]="fail";
    f.repackage(contents);
    const auto root=f.root();write(root+"/ParallelFinder.exe",stub);write(root+"/version.txt","0.1.0");
    QProcess parent;parent.start(root+"/ParallelFinder.exe",{"--wait"});QVERIFY(parent.waitForStarted());
    const auto request=f.directory.path()+"/request.json";
    if(cancelHandoff)write(request+".cancel","cancel");
    write(request,QJsonDocument(QJsonObject{{"schema",1},{"root",root},{"package",f.package()},{"manifest",QString::fromLatin1(f.bytes.toBase64())},{"signature",QString::fromLatin1(f.signature.toBase64())},{"testPublicKey",QString::fromLatin1(f.publicKey.toBase64())},{"currentVersion","0.1.0"},{"parentPid",parent.processId()},{"consent",true}}).toJson());
    QProcess worker;worker.start(PF_UPDATE_TEST_WORKER,{"--request",request});QVERIFY(worker.waitForStarted());QVERIFY(parent.waitForFinished(5000));QVERIFY(worker.waitForFinished(15000));
    QCOMPARE(worker.exitCode(),cancelHandoff?4:failHealth?5:0);QCOMPARE(read(root+"/version.txt"),QByteArray(failHealth||cancelHandoff?"0.1.0":"1.0.0"));
    QCOMPARE(QJsonDocument::fromJson(read(f.directory.path()+"/last-result.json")).object()["success"].toBool(),!failHealth&&!cancelHandoff);
    QTest::qWait(600);
}
void UpdateTests::productionUpdaterRejectsTestKey() {
    Fixture f;const auto root=f.root();const auto request=f.directory.path()+"/unsigned-request.json";
    write(request,QJsonDocument(QJsonObject{{"schema",1},{"root",root},{"package",f.package()},{"manifest",QString::fromLatin1(f.bytes.toBase64())},{"signature",QString::fromLatin1(f.signature.toBase64())},{"testPublicKey",QString::fromLatin1(f.publicKey.toBase64())},{"currentVersion","0.1.0"},{"parentPid",QCoreApplication::applicationPid()},{"consent",true}}).toJson());
    QProcess worker;worker.start(PF_UPDATE_PRODUCTION_WORKER,{"--request",request});QVERIFY(worker.waitForStarted());QVERIFY(worker.waitForFinished(5000));QCOMPARE(worker.exitCode(),3);QCOMPARE(read(root+"/ParallelFinder.exe"),QByteArray("old-application"));
}
void UpdateTests::lockedDirectoryRollsBack() {
    Fixture f;const auto root=f.root();QString error;HANDLE held=INVALID_HANDLE_VALUE;
    pfupdate::InstallHooks hooks;hooks.beforeActivate=[&](QString&){held=CreateFileW(reinterpret_cast<LPCWSTR>(root.utf16()),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_EXISTING,FILE_FLAG_BACKUP_SEMANTICS,nullptr);return held!=INVALID_HANDLE_VALUE;};
    pfupdate::UpdateTransaction transaction(root);const bool installed=transaction.install(f.package(),f.manifest,hooks,error);
    if(held!=INVALID_HANDLE_VALUE)CloseHandle(held);
    QVERIFY(!installed);QCOMPARE(read(root+"/ParallelFinder.exe"),QByteArray("old-application"));QVERIFY(!QFileInfo::exists(root+".pf-stage"));
}
#endif
QTEST_MAIN(UpdateTests)
#include "test_updates.moc"
