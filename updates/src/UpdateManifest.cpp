#include <pfupdate/UpdateManifest.hpp>
#include "UpdateTrust.hpp"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <memory>

namespace pfupdate {
QByteArray trustedPublicKey() { return pfUpdatePublicKey; }
namespace {
struct Version { QVector<quint64> core; QStringList pre; };
std::optional<Version> parse(QString value) {
    if(value.startsWith('v'))value.remove(0,1);
    const QRegularExpression re("^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z-]+(?:\\.[0-9A-Za-z-]+)*))?(?:\\+[0-9A-Za-z.-]+)?$");
    const auto m=re.match(value);if(!m.hasMatch())return {};
    Version v;
    for(int i=1;i<=3;++i) {bool ok=false;auto n=m.captured(i).toULongLong(&ok);if(!ok)return {};v.core.push_back(n);}
    if(!m.captured(4).isEmpty())v.pre=m.captured(4).split('.');
    for(const auto& p:v.pre)if(QRegularExpression("^[0-9]+$").match(p).hasMatch() && p.size()>1 && p[0]=='0')return {};
    return v;
}
bool hashValid(const QString& s) {return QRegularExpression("^[a-f0-9]{64}$").match(s).hasMatch();}
}
std::optional<int> compareVersions(QString left, QString right) {
    const auto a=parse(left),b=parse(right);if(!a||!b)return {};
    for(int i=0;i<3;++i)if(a->core[i]!=b->core[i])return a->core[i]<b->core[i]?-1:1;
    if(a->pre.isEmpty()!=b->pre.isEmpty())return a->pre.isEmpty()?1:-1;
    for(int i=0;i<std::min(a->pre.size(),b->pre.size());++i) {
        const auto& x=a->pre[i];const auto& y=b->pre[i];if(x==y)continue;
        const bool nx=QRegularExpression("^[0-9]+$").match(x).hasMatch(),ny=QRegularExpression("^[0-9]+$").match(y).hasMatch();
        if(nx!=ny)return nx?-1:1;
        if(nx && x.size()!=y.size())return x.size()<y.size()?-1:1;
        return x<y?-1:1;
    }
    return a->pre.size()==b->pre.size()?0:a->pre.size()<b->pre.size()?-1:1;
}
bool safeRelativePath(const QString& path) {
    if(path.isEmpty() || path.size()>240 || path.contains('\\') || path.contains(':') || path.startsWith('/') || path.contains(QChar(0)))return false;
    for(const auto& part:path.split('/')) {
        if(part.isEmpty() || part=="." || part==".." || part.endsWith('.') || part.endsWith(' '))return false;
        if(QRegularExpression("[<>\"|?*\\x00-\\x1f]").match(part).hasMatch())return false;
        if(QRegularExpression("^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\\.|$)",QRegularExpression::CaseInsensitiveOption).match(part).hasMatch())return false;
    }
    return true;
}
std::optional<QString> restartAcknowledgementVersion(const QByteArray& request,
    const QString& currentVersion,const QString& installationDirectory,const QByteArray& publicKey) {
    if(request.size()>4*1024*1024)return {};
    const auto o=QJsonDocument::fromJson(request).object();QString error;
    const auto root=QFileInfo(o["root"].toString()).canonicalFilePath();
    if(o["schema"].toInt()!=1 || !o["consent"].toBool() || root.isEmpty()
        || root!=QFileInfo(installationDirectory).canonicalFilePath())return {};
    const auto manifest=verifyManifest(QByteArray::fromBase64(o["manifest"].toString().toLatin1()),
        QByteArray::fromBase64(o["signature"].toString().toLatin1()),publicKey,error);
    if(!manifest || compareVersions(manifest->version,currentVersion).value_or(-1)!=0)return {};
    return manifest->version;
}
std::optional<Manifest> verifyManifest(const QByteArray& bytes,const QByteArray& signature,const QByteArray& publicKey,QString& error) {
    auto fail=[&](QString message)->std::optional<Manifest>{error=message;return {};};
    if(bytes.isEmpty() || bytes.size()>2*1024*1024 || signature.size()!=64)return fail("Invalid signed manifest size");
    std::unique_ptr<BIO,decltype(&BIO_free)> bio(BIO_new_mem_buf(publicKey.constData(),static_cast<int>(publicKey.size())),BIO_free);
    std::unique_ptr<EVP_PKEY,decltype(&EVP_PKEY_free)> key(bio?PEM_read_bio_PUBKEY(bio.get(),nullptr,nullptr,nullptr):nullptr,EVP_PKEY_free);
    std::unique_ptr<EVP_MD_CTX,decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(),EVP_MD_CTX_free);
    if(!key || EVP_PKEY_base_id(key.get())!=EVP_PKEY_ED25519 || !ctx
        || EVP_DigestVerifyInit(ctx.get(),nullptr,nullptr,nullptr,key.get())!=1
        || EVP_DigestVerify(ctx.get(),reinterpret_cast<const unsigned char*>(signature.constData()),signature.size(),reinterpret_cast<const unsigned char*>(bytes.constData()),bytes.size())!=1)
        return fail("Update signature is not trusted");
    QJsonParseError parseError;const auto doc=QJsonDocument::fromJson(bytes,&parseError);const auto o=doc.object();
    if(parseError.error!=QJsonParseError::NoError || o["schema"].toInt()!=1 || o["repository"]!="Pozit1vchic/Parallel-Finder" || o["platform"]!="windows-x64")return fail("Unsupported update manifest");
    Manifest m;m.version=o["version"].toString();m.channel=o["channel"].toString();m.asset=o["asset"].toString();m.size=o["size"].toInteger();m.changelog=o["changelog"].toString().left(5000);
    if(!parse(m.version) || (m.channel!="stable" && m.channel!="beta") || (m.channel=="stable" && !parse(m.version)->pre.isEmpty())
        || !safeRelativePath(m.asset) || m.asset.contains('/') || !m.asset.endsWith("-Portable-x64.zip") || m.size<=0 || m.size>2LL*1024*1024*1024 || !hashValid(o["sha256"].toString()))return fail("Invalid update metadata");
    m.sha256=QByteArray::fromHex(o["sha256"].toString().toLatin1());
    const auto rows=o["files"].toArray();if(rows.isEmpty() || rows.size()>10000)return fail("Invalid update file inventory");
    QSet<QString> seen;qint64 total=0;
    for(const auto& row:rows) {
        const auto f=row.toObject();FileRecord file{f["path"].toString(),f["size"].toInteger(-1),QByteArray::fromHex(f["sha256"].toString().toLatin1())};
        if(!safeRelativePath(file.path) || seen.contains(file.path.toCaseFolded()) || file.size<0 || file.size>512LL*1024*1024 || !hashValid(f["sha256"].toString()))return fail("Unsafe update file inventory");
        seen.insert(file.path.toCaseFolded());total+=file.size;if(total>4LL*1024*1024*1024)return fail("Update is too large");m.files.push_back(file);
    }
    if(!seen.contains("parallelfinder.exe") || !seen.contains("parallelfinderupdater.exe"))return fail("Missing application or updater");
    for(const auto& f:m.files)for(QString parent=f.path.section('/',0,-2);f.path.contains('/') && !parent.isEmpty();parent=parent.contains('/')?parent.section('/',0,-2):QString{})
        if(seen.contains(parent.toCaseFolded()))return fail("Conflicting update paths");
    return m;
}
bool verifyPackage(const QString& path,const Manifest& m,QString& error) {
    QFile f(path);if(!f.open(QIODevice::ReadOnly) || f.size()!=m.size) {error="Update size mismatch";return false;}
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if(!hash.addData(&f) || hash.result()!=m.sha256){error="Update checksum mismatch";return false;}
    return true;
}
}
