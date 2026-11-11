#pragma once
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>
#include <optional>
#include <functional>

namespace pfupdate {
struct FileRecord { QString path; qint64 size=0; QByteArray sha256; };
struct IncrementalPackage {
    QString baseVersion, asset;
    qint64 size=0;
    QByteArray sha256;
    QStringList reuse;
};
struct Manifest {
    QString version, channel, asset, changelog;
    qint64 size=0;
    QByteArray sha256;
    QVector<FileRecord> files;
    QVector<IncrementalPackage> incremental;
    // Selected only from authenticated incremental metadata.
    QStringList reuse;
    QString baseVersion;
};
QByteArray trustedPublicKey();
// SemVer, including numeric prerelease components (rc.16.4.1).
std::optional<int> compareVersions(QString left, QString right);
bool safeRelativePath(const QString& path);
std::optional<Manifest> verifyManifest(const QByteArray& bytes, const QByteArray& signature,
    const QByteArray& publicKey, QString& error,
    const QString& selectedAsset={}, const QString& currentVersion={});
// Check every reused file against the signed target inventory before selection.
bool canReuseFiles(const QString& root, const Manifest& manifest, const QStringList& paths,
    const std::function<bool()>& cancelled={});
bool verifyPackage(const QString& path, const Manifest& manifest, QString& error);
std::optional<QString> restartAcknowledgementVersion(const QByteArray& request,
    const QString& currentVersion, const QString& installationDirectory, const QByteArray& publicKey);
}
