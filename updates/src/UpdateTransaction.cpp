#include <pfupdate/UpdateTransaction.hpp>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QSet>
#include <archive.h>
#include <archive_entry.h>
#include <memory>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace pfupdate {
namespace {
bool renameDirectory(const QString& from,const QString& to) {
#ifdef Q_OS_WIN
    return MoveFileExW(reinterpret_cast<LPCWSTR>(from.utf16()),reinterpret_cast<LPCWSTR>(to.utf16()),MOVEFILE_WRITE_THROUGH)!=0;
#else
    return QDir().rename(from,to);
#endif
}
bool link(const QFileInfo& info) {
#ifdef Q_OS_WIN
    const auto attrs=GetFileAttributesW(reinterpret_cast<LPCWSTR>(info.absoluteFilePath().utf16()));
    if(attrs!=INVALID_FILE_ATTRIBUTES && (attrs&FILE_ATTRIBUTE_REPARSE_POINT))return true;
#endif
    return info.isSymLink();
}
bool preserveFiles(const QString& root,const QString& stage,QString& error) {
    QDirIterator it(root,QDir::AllEntries|QDir::NoDotAndDotDot|QDir::Hidden|QDir::System,QDirIterator::Subdirectories);
    while(it.hasNext()) {
        it.next();const auto f=it.fileInfo();
        if(link(f)){error="Linked files or directories are not supported in automatic updates";return false;}
        const auto destination=stage+'/'+QDir(root).relativeFilePath(f.absoluteFilePath());
        if(f.isDir()) {if(!QDir().mkpath(destination)){error="Cannot preserve installation directories";return false;}}
        else if(!QFileInfo::exists(destination) && !QFile::copy(f.absoluteFilePath(),destination)){error="Cannot preserve local installation files";return false;}
    }
    return true;
}
bool extract(const QString& package,const QString& stage,const Manifest& manifest,QString& error) {
    std::unique_ptr<archive,decltype(&archive_read_free)> reader(archive_read_new(),archive_read_free);
    archive_read_support_format_zip(reader.get());archive_read_support_filter_none(reader.get());
#ifdef Q_OS_WIN
    const auto opened=archive_read_open_filename_w(reader.get(),reinterpret_cast<const wchar_t*>(package.utf16()),64*1024);
#else
    const auto opened=archive_read_open_filename(reader.get(),package.toUtf8().constData(),64*1024);
#endif
    if(opened!=ARCHIVE_OK){error="Cannot open update archive";return false;}
    QMap<QString,FileRecord> expected;for(const auto& f:manifest.files)expected.insert(f.path,f);
    QSet<QString> extracted;
    archive_entry* entry=nullptr;int status;
    while((status=archive_read_next_header(reader.get(),&entry))==ARCHIVE_OK) {
        const auto raw=QString::fromUtf8(archive_entry_pathname_utf8(entry));
        if(!raw.startsWith("ParallelFinder/")){error="Unexpected archive root";return false;}
        auto path=raw.mid(15);if(path.endsWith('/'))path.chop(1);
        if(path.isEmpty() && archive_entry_filetype(entry)==AE_IFDIR)continue;
        if(!safeRelativePath(path) || archive_entry_symlink(entry) || archive_entry_hardlink(entry)) {error="Unsafe archive entry";return false;}
        if(archive_entry_filetype(entry)==AE_IFDIR) {
            bool allowed=false;for(const auto& f:manifest.files)if(f.path.startsWith(path+'/')){allowed=true;break;}
            if(!allowed || !QDir().mkpath(stage+'/'+path)){error="Unexpected archive directory";return false;}continue;
        }
        if(archive_entry_filetype(entry)!=AE_IFREG || !expected.contains(path) || extracted.contains(path.toCaseFolded())) {error="Unexpected archive file";return false;}
        const auto& f=expected[path];if(archive_entry_size(entry)!=f.size){error="Archive file size mismatch";return false;}
        const auto target=stage+'/'+path;if(!QDir().mkpath(QFileInfo(target).absolutePath())){error="Cannot create update directory";return false;}
        QSaveFile file(target);if(!file.open(QIODevice::WriteOnly)){error="Cannot write staged update";return false;}
        QCryptographicHash hash(QCryptographicHash::Sha256);qint64 written=0;char block[64*1024];la_ssize_t n;
        while((n=archive_read_data(reader.get(),block,sizeof block))>0) {
            written+=n;if(written>f.size || file.write(block,n)!=n){error="Update extraction failed";return false;}hash.addData(QByteArrayView(block,n));
        }
        if(n<0 || written!=f.size || hash.result()!=f.sha256 || !file.commit()){error="Staged update checksum mismatch";return false;}
        extracted.insert(path.toCaseFolded());
    }
    if(status!=ARCHIVE_EOF || extracted.size()!=expected.size()){error="Incomplete update archive";return false;}
    return true;
}
}
UpdateTransaction::UpdateTransaction(QString installationRoot)
    :root_(QDir::cleanPath(QFileInfo(installationRoot).absoluteFilePath())),stage_(root_+".pf-stage"),backup_(root_+".pf-backup") {}
QString UpdateTransaction::journalPath() const {return root_+".pf-update.json";}
bool UpdateTransaction::journal(const QString& phase,QString& error) {
    QSaveFile file(journalPath());if(!file.open(QIODevice::WriteOnly)){error="Cannot persist update transaction";return false;}
    const auto bytes=QJsonDocument(QJsonObject{{"schema",1},{"root",root_},{"phase",phase}}).toJson(QJsonDocument::Compact);
    if(file.write(bytes)!=bytes.size() || !file.commit()){error="Cannot persist update transaction";return false;}return true;
}
bool UpdateTransaction::rollback(QString& error) {
    if(QFileInfo::exists(backup_)) {
        if(QFileInfo::exists(root_)) {
            if(QFileInfo::exists(stage_) && !QDir(stage_).removeRecursively()){error="Cannot remove staged update";return false;}
            if(!renameDirectory(root_,stage_)){error="Cannot move failed installation for rollback";return false;}
        }
        if(!renameDirectory(backup_,root_)){error="Cannot restore previous installation; backup retained";return false;}
    }
    if(QFileInfo::exists(stage_) && !QDir(stage_).removeRecursively()){error="Rollback restored application; staging cleanup failed";return false;}
    QFile::remove(journalPath());return true;
}
bool UpdateTransaction::recover(QString& error) {
    QFile file(journalPath());if(!file.exists())return true;
    if(!file.open(QIODevice::ReadOnly) || file.size()>4096){error="Cannot read recovery journal";return false;}
    const auto doc=QJsonDocument::fromJson(file.readAll()).object();
    if(doc["schema"].toInt()!=1 || doc["root"].toString()!=root_){error="Invalid recovery journal";return false;}
    const auto phase=doc["phase"].toString();
    if(phase=="committed") {QFile::remove(journalPath());return true;}
    if(phase!="staging" && phase!="prepared" && phase!="activating" && phase!="activated"){error="Invalid recovery phase";return false;}
    return rollback(error);
}
bool UpdateTransaction::install(const QString& package,const Manifest& manifest,const InstallHooks& hooks,QString& error) {
    QLockFile lock(root_+".pf-update.lock");lock.setStaleLockTime(0);
    if(!lock.tryLock(0)){error="Another updater is running";return false;}
    if(root_==QDir(root_).rootPath() || root_.isEmpty() || link(QFileInfo(root_)) || !QFileInfo(root_+"/ParallelFinder.exe").isFile()) {error="Invalid installation root";return false;}
    if(!recover(error) || !verifyPackage(package,manifest,error))return false;
    // Deterministic sibling paths are exclusively owned by this transaction;
    // refuse pre-existing staging/backup instead of deleting unknown data.
    if(QFileInfo::exists(stage_) || QFileInfo::exists(backup_)){error="Previous backup exists; keep it or move it before updating";return false;}
    if(!journal("staging",error) || !QDir().mkpath(stage_))return false;
    auto fail=[&]{QString rollbackError;if(!rollback(rollbackError))error+="; "+rollbackError;return false;};
    if(!extract(package,stage_,manifest,error) || !preserveFiles(root_,stage_,error))return fail();
    if(hooks.healthCheck && !hooks.healthCheck(stage_+"/ParallelFinder.exe",error))return fail();
    if(!journal("prepared",error) || (hooks.beforeActivate && !hooks.beforeActivate(error)) || !journal("activating",error))return fail();
    if(!renameDirectory(root_,backup_)){error="Installation is locked or not writable";return fail();}
    if(!renameDirectory(stage_,root_)){error="Cannot activate update";return fail();}
    if(!journal("activated",error) || (hooks.restart && !hooks.restart(root_+"/ParallelFinder.exe",error)))return fail();
    if(!journal("committed",error))return fail();
    // A healthy restart is acknowledged before the old version is removed.
    // Failure to clean a backup is harmless; retain it instead of corrupting
    // the active installation. A subsequent update refuses to overwrite it.
    if(QDir(backup_).removeRecursively())QFile::remove(journalPath());
    return true;
}
}
