#include <pfupdate/UpdateManifest.hpp>
#include <pfupdate/UpdateTransaction.hpp>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QProcess>
#include <QSaveFile>
#include <QThread>
#include <QElapsedTimer>
#include <QUuid>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace {
void record(const QString& directory,bool success,const QString& error) {
    QSaveFile file(directory+"/last-result.json");
    if(file.open(QIODevice::WriteOnly)){file.write(QJsonDocument(QJsonObject{{"success",success},{"error",error}}).toJson());file.commit();}
}
#ifdef Q_OS_WIN
bool recoveryRegistration(const QString& root,bool remove) {
    HKEY key=nullptr;
    if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce",0,nullptr,0,KEY_SET_VALUE,nullptr,&key,nullptr)!=ERROR_SUCCESS)return false;
    LONG result;
#ifdef PF_UPDATER_TESTS
    constexpr auto valueName=L"ParallelFinderUpdateRecoveryTest";
#else
    constexpr auto valueName=L"ParallelFinderUpdateRecovery";
#endif
    if(remove)result=RegDeleteValueW(key,valueName);
    else {
        const auto command='"'+QCoreApplication::applicationFilePath()+"\" --recover \""+root+'"';
        result=RegSetValueExW(key,valueName,0,REG_SZ,reinterpret_cast<const BYTE*>(command.utf16()),(command.size()+1)*sizeof(wchar_t));
    }
    RegCloseKey(key);return result==ERROR_SUCCESS || (remove && result==ERROR_FILE_NOT_FOUND);
}
#endif
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);const auto args=app.arguments();QString error;
    if(args.size()==3 && args[1]=="--recover") {
        const auto root=QDir::cleanPath(QFileInfo(args[2]).absoluteFilePath());
        QLockFile lock(root+".pf-update.lock");lock.setStaleLockTime(0);if(!lock.tryLock(0))return 2;
        pfupdate::UpdateTransaction transaction(root);const bool ok=transaction.recover(error);
#ifdef Q_OS_WIN
        if(ok)recoveryRegistration(root,true);
#endif
        record(QFileInfo(QCoreApplication::applicationDirPath()).absolutePath(),ok,error);
        if(ok && QFileInfo(root+"/ParallelFinder.exe").isFile())QProcess::startDetached(root+"/ParallelFinder.exe",{},root);
        return ok?0:3;
    }
    if(args.size()!=3 || args[1]!="--request")return 2;
    QFile request(args[2]);if(!request.open(QIODevice::ReadOnly) || request.size()>4*1024*1024)return 2;
    const auto o=QJsonDocument::fromJson(request.readAll()).object();
    const auto storage=QFileInfo(args[2]).absolutePath();
    const auto root=QDir::cleanPath(o["root"].toString());
    auto trustKey=pfupdate::trustedPublicKey();
#ifdef PF_UPDATER_TESTS
    // Compiled only into the separate test worker; production has no override.
    trustKey=QByteArray::fromBase64(o["testPublicKey"].toString().toLatin1());
#endif
    const auto manifest=pfupdate::verifyManifest(QByteArray::fromBase64(o["manifest"].toString().toLatin1()),QByteArray::fromBase64(o["signature"].toString().toLatin1()),trustKey,error);
    if(o["schema"].toInt()!=1 || !o["consent"].toBool() || !manifest || !QFileInfo(root).isAbsolute()
        || pfupdate::compareVersions(manifest?manifest->version:QString{},o["currentVersion"].toString()).value_or(-1)<=0
        || !pfupdate::verifyPackage(o["package"].toString(),*manifest,error)) {record(storage,false,error.isEmpty()?"Invalid updater request":error);return 3;}
#ifdef Q_OS_WIN
    const auto pid=o["parentPid"].toInteger();if(pid<=0 || pid>MAXDWORD){record(storage,false,"Invalid parent process");return 3;}
    HANDLE parent=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,static_cast<DWORD>(pid));
    const auto ready=[&] {
        if(QFile::exists(args[2]+".cancel"))return false;
        QSaveFile file(args[2]+".ready");return file.open(QIODevice::WriteOnly)
            && file.write(manifest->version.toUtf8())==manifest->version.toUtf8().size() && file.commit();
    };
    if(parent) {
        wchar_t filename[32768];DWORD size=32768;
        const bool expected=QueryFullProcessImageNameW(parent,0,filename,&size)
            && QFileInfo(QString::fromWCharArray(filename,size)).canonicalFilePath().compare(QFileInfo(root+"/ParallelFinder.exe").canonicalFilePath(),Qt::CaseInsensitive)==0;
        if(!expected || !ready()){CloseHandle(parent);record(storage,false,"Updater handoff failed");return 4;}
        QElapsedTimer wait;wait.start();DWORD status=WAIT_TIMEOUT;
        while(wait.elapsed()<60000 && !QFile::exists(args[2]+".cancel")) {
            status=WaitForSingleObject(parent,100);if(status!=WAIT_TIMEOUT)break;
        }
        if(status!=WAIT_OBJECT_0){CloseHandle(parent);record(storage,false,"Main application did not close safely");return 4;}CloseHandle(parent);
    } else if(!ready()) {
        record(storage,false,"Updater handoff cancelled");return 4;
    }
    if(!recoveryRegistration(root,false)){record(storage,false,"Cannot register crash recovery");return 4;}
#endif
    pfupdate::InstallHooks hooks;
    hooks.healthCheck=[](const QString& executable,QString& error) {
        QProcess process;process.setProgram(executable);process.setArguments({"--pf-update-healthcheck"});process.setWorkingDirectory(QFileInfo(executable).absolutePath());
#ifdef Q_OS_WIN
        process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args){args->startupInfo->dwFlags|=STARTF_USESHOWWINDOW;args->startupInfo->wShowWindow=SW_HIDE;});
#endif
        process.start();if(!process.waitForStarted(10000) || !process.waitForFinished(30000) || process.exitStatus()!=QProcess::NormalExit || process.exitCode()!=0) {
            process.kill();process.waitForFinished(5000);error="New application startup check failed";return false;
        }return true;
    };
    hooks.restart=[&](const QString& executable,QString& error) {
        const auto ack=storage+"/ack-"+QUuid::createUuid().toString(QUuid::WithoutBraces)+".txt";qint64 pid=0;
        if(!QProcess::startDetached(executable,{"--pf-update-ack",ack},QFileInfo(executable).absolutePath(),&pid)){error="Cannot restart updated application";return false;}
        QElapsedTimer timer;timer.start();
        while(timer.elapsed()<30000) {
            QFile file(ack);if(file.open(QIODevice::ReadOnly) && pfupdate::compareVersions(QString::fromUtf8(file.read(256)),manifest->version).value_or(-1)==0){file.close();QFile::remove(ack);return true;}
            QThread::msleep(100);
        }
#ifdef Q_OS_WIN
        HANDLE child=OpenProcess(PROCESS_TERMINATE|SYNCHRONIZE,FALSE,static_cast<DWORD>(pid));if(child){TerminateProcess(child,5);WaitForSingleObject(child,10000);CloseHandle(child);}
#endif
        error="Restart was not acknowledged; restoring previous version";return false;
    };
    pfupdate::UpdateTransaction transaction(root);const bool ok=transaction.install(o["package"].toString(),*manifest,hooks,error);
    record(storage,ok,error);
    QFile::remove(args[2]+".ready");
#ifdef Q_OS_WIN
    if(!QFileInfo::exists(transaction.journalPath()))recoveryRegistration(root,true);
#endif
    if(!ok && QFileInfo(root+"/ParallelFinder.exe").isFile())QProcess::startDetached(root+"/ParallelFinder.exe",{},root);
    if(ok){QFile::remove(o["package"].toString());QFile::remove(args[2]);}
    return ok?0:5;
}
