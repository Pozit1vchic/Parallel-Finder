#include <pfservices/RuntimeScratch.hpp>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QTimer>

namespace pfservices {
namespace {
void removeOwnedDirectory(const QString& path, int retries)
{
    if (!QFileInfo::exists(path) || QDir(path).removeRecursively()) return;
    // Windows readers can hold a PNG open briefly during an asynchronous
    // image replacement. Bound retries; abandoned sessions retain startup cleanup.
    if (retries > 0 && QCoreApplication::instance() && !QCoreApplication::closingDown())
        QTimer::singleShot(250, QCoreApplication::instance(), [path, retries] {
            removeOwnedDirectory(path, retries - 1);
        });
}
}

RuntimeScratch::RuntimeScratch(const QString& root, int retentionSeconds)
{
    if (QFileInfo(root).isSymLink() || !QDir().mkpath(root)) return;
    const auto expiry = QDateTime::currentDateTimeUtc().addSecs(-qMax(0, retentionSeconds));
    const QRegularExpression session("^pf-session-[A-Za-z0-9]{6}$");
    const QRegularExpression legacy("^[0-9]+_(?:[0-9]+_(?:start|end)|match_frame_[0-9]+)\\.png$");
    for (const auto& entry : QDir(root).entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (entry.isSymLink() || entry.lastModified().toUTC() >= expiry) continue;
        if (entry.isFile() && legacy.match(entry.fileName()).hasMatch()) {
            QFile::remove(entry.absoluteFilePath());
        } else if (entry.isDir() && session.match(entry.fileName()).hasMatch()) {
            QLockFile lock(entry.absoluteFilePath() + "/.owner.lock");
            lock.setStaleLockTime(0);
            if (lock.tryLock(0)) {
                // Windows cannot remove an open lock file; session paths are
                // never reused, and an active owner would have denied locking.
                lock.unlock();
                QDir(entry.absoluteFilePath()).removeRecursively();
            }
        }
    }
    directory_ = std::make_unique<QTemporaryDir>(QDir(root).absoluteFilePath("pf-session-XXXXXX"));
    if (!directory_->isValid()) { directory_.reset(); return; }
    owner_ = std::make_unique<QLockFile>(directory_->filePath(".owner.lock"));
    owner_->setStaleLockTime(0);
    if (!owner_->tryLock(0)) { owner_.reset(); directory_.reset(); }
}
RuntimeScratch::~RuntimeScratch()
{
    owner_.reset();
    if (!directory_) return;
    const auto ownedPath = directory_->path();
    directory_->setAutoRemove(false);
    directory_.reset();
    if (!ownedPath.isEmpty()) removeOwnedDirectory(ownedPath, 8);
}
QString RuntimeScratch::path() const { return directory_ ? directory_->path() : QString{}; }
}
