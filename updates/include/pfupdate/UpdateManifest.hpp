#pragma once
#include <QByteArray>
#include <QString>
#include <QVector>
#include <optional>

namespace pfupdate {
struct FileRecord { QString path; qint64 size=0; QByteArray sha256; };
struct Manifest {
    QString version, channel, asset, changelog;
    qint64 size=0;
    QByteArray sha256;
    QVector<FileRecord> files;
};
QByteArray trustedPublicKey();
// SemVer, including numeric prerelease components (rc.16.4.1).
std::optional<int> compareVersions(QString left, QString right);
bool safeRelativePath(const QString& path);
std::optional<Manifest> verifyManifest(const QByteArray& bytes, const QByteArray& signature,
    const QByteArray& publicKey, QString& error);
bool verifyPackage(const QString& path, const Manifest& manifest, QString& error);
std::optional<QString> restartAcknowledgementVersion(const QByteArray& request,
    const QString& currentVersion, const QString& installationDirectory, const QByteArray& publicKey);
}
