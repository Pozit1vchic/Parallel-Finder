#include "ProviderDownload.hpp"
#include "NetworkOperation.hpp"
#include <pfservices/ProviderStore.hpp>

#include <QEventLoop>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <archive.h>
#include <archive_entry.h>
#include <QSet>
#include <memory>
#include <QTimer>
#include <QTemporaryDir>
#include <QSaveFile>
#include <QLockFile>
#include <QCryptographicHash>
#include <QUrl>

#include <algorithm>
#include <cstdlib>
#include <filesystem>

#include <pfservices/ModelStore.hpp>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace pfservices {
namespace {

constexpr qsizetype kMaxManifestBytes = 2 * 1024 * 1024;

bool safeProvider(const std::string& provider)
{
    return provider == "dml" || provider == "cuda" || provider == "tensorrt" || provider == "cpu";
}

bool safeArchive(const std::string& archive)
{
    const std::filesystem::path path(archive);
    return !archive.empty() && archive.front() != '-' && archive.size() <= 255 && path.filename() == path
        && !path.has_root_path() && !path.has_parent_path()
        && archive.find("..") == std::string::npos;
}

bool safeArchiveMember(const QString& rawName)
{
    QString name = QDir::fromNativeSeparators(rawName.trimmed());
    if (name.isEmpty() || name.startsWith(QLatin1Char('/'))
        || (name.size() >= 2 && name.at(1) == QLatin1Char(':')))
        return false;
    const QStringList components = name.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString& component : components) {
        if (component==".." || component=="." || component.contains(QChar(0)) || component.contains(':')
            || component.endsWith('.') || component.endsWith(' ')) return false;
    }
    return !components.isEmpty();
}

bool trustedHost(const QUrl& url)
{
    const QString host = url.host().trimmed().toLower();
    return !host.isEmpty()
        && (host == QStringLiteral("github.com")
            || host == QStringLiteral("raw.githubusercontent.com")
            || host == QStringLiteral("objects.githubusercontent.com")
            || host == QStringLiteral("githubusercontent.com")
            || host.endsWith(QStringLiteral(".github.com"))
            || host.endsWith(QStringLiteral(".githubusercontent.com")));
}

bool validateExtractedTree(const std::filesystem::path& root, std::string& error)
{
    std::error_code filesystemError;
    if (!std::filesystem::is_directory(root, filesystemError)) {
        error = "provider archive did not create a directory";
        return false;
    }
    bool runtimeFound = false;
    std::size_t visited = 0;
    const auto options = std::filesystem::directory_options::skip_permission_denied;
    for (std::filesystem::recursive_directory_iterator it(root, options, filesystemError), end;
         it != end && !filesystemError; it.increment(filesystemError)) {
        if (++visited > 1024) {
            error = "provider archive contains too many files";
            return false;
        }
        if (it->is_symlink(filesystemError)) {
            error = "provider archive created a symbolic link";
            return false;
        }
        if (it->is_regular_file(filesystemError)
            && it->path().filename() == std::filesystem::path("onnxruntime.dll")) {
            runtimeFound = true;
        }
    }
    if (filesystemError) {
        error = "inspect extracted provider runtime: " + filesystemError.message();
        return false;
    }
    if (!runtimeFound) {
        error = "provider archive does not contain onnxruntime.dll";
        return false;
    }
    return true;
}

std::optional<ProviderAsset> parseManifest(const QByteArray& bytes,
                                           const std::string& provider,
                                           std::string& error)
{
    if (bytes.size() > kMaxManifestBytes) {
        error = "provider manifest is too large";
        return std::nullopt;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        error = "parse provider manifest: " + parseError.errorString().toStdString();
        return std::nullopt;
    }
    const QJsonValue value = document.object().value(QStringLiteral("providers"));
    if (!value.isArray()) {
        error = "provider manifest has no providers array";
        return std::nullopt;
    }
    for (const QJsonValue& item : value.toArray()) {
        if (!item.isObject()) continue;
        const QJsonObject object = item.toObject();
        if (object.value(QStringLiteral("provider")).toString().toLower()
            != QString::fromStdString(provider).toLower()) continue;
        ProviderAsset asset;
        asset.provider = provider;
        asset.archive = object.value(QStringLiteral("archive")).toString().toStdString();
        asset.sha256 = object.value(QStringLiteral("sha256")).toString().toStdString();
        asset.sizeBytes = static_cast<std::uint64_t>(std::max<qint64>(
            0, object.value(QStringLiteral("sizeBytes")).toInteger()));
        if (object.value(QStringLiteral("downloadUrl")).isString())
            asset.downloadUrl = object.value(QStringLiteral("downloadUrl")).toString().toStdString();
        if (object.contains("parts")) {
            const auto parts = object.value("parts").toArray();
            if (parts.isEmpty() || parts.size() > 16) { error = "invalid provider parts count"; return std::nullopt; }
            std::uint64_t total = 0;
            for (const auto& value : parts) {
                const auto part = value.toObject();
                ProviderPart item{part.value("archive").toString().toStdString(),
                    part.value("sha256").toString().toStdString(),
                    static_cast<std::uint64_t>(std::max<qint64>(0, part.value("sizeBytes").toInteger())),
                    part.value("downloadUrl").toString().toStdString()};
                if (!safeArchive(item.archive) || !item.sizeBytes || item.sizeBytes >= 2ULL*1024*1024*1024
                    || item.sha256.size() != 64 || item.downloadUrl.empty()
                    || std::any_of(asset.parts.begin(), asset.parts.end(), [&](const auto& existing) { return existing.archive == item.archive; })) {
                    error = "invalid provider part"; return std::nullopt;
                }
                total += item.sizeBytes;
                asset.parts.push_back(std::move(item));
            }
            if (total != asset.sizeBytes || total > 8ULL*1024*1024*1024 || asset.sha256.size() != 64) {
                error = "provider parts size/hash metadata mismatch"; return std::nullopt;
            }
        }
        if (!safeArchive(asset.archive) || (asset.downloadUrl.empty() && asset.parts.empty())) {
            error = "provider manifest contains an unsafe or incomplete archive entry";
            return std::nullopt;
        }
        return asset;
    }
    error = "provider '" + provider + "' is absent from manifest";
    return std::nullopt;
}

bool extractArchive(const std::filesystem::path& archive,
                    const std::filesystem::path& destination,
                    std::string& error)
{
    std::unique_ptr<struct archive,decltype(&archive_read_free)> reader(archive_read_new(),archive_read_free);
    archive_read_support_format_zip(reader.get());
    archive_read_support_filter_none(reader.get());
#ifdef Q_OS_WIN
    const auto opened=archive_read_open_filename_w(reader.get(),archive.c_str(),64*1024);
#else
    const auto opened=archive_read_open_filename(reader.get(),archive.c_str(),64*1024);
#endif
    if (opened!=ARCHIVE_OK) {error="PROVIDER_ARCHIVE_OPEN_FAILED";return false;}
    const auto root=QString::fromStdWString(destination.wstring());
    if (!QDir().mkpath(root)) {error="PROVIDER_STAGING_CREATE_FAILED";return false;}
    QSet<QString> names;
    std::uint64_t extracted=0;
    constexpr std::uint64_t maximumExtracted=16ULL*1024*1024*1024;
    archive_entry* entry=nullptr;int status;
    while ((status=archive_read_next_header(reader.get(),&entry))==ARCHIVE_OK) {
        if (QThread::currentThread()->isInterruptionRequested()) {error="PROVIDER_INSTALL_CANCELLED";return false;}
        const auto* utf8=archive_entry_pathname_utf8(entry);
        if (!utf8) {error="PROVIDER_ARCHIVE_PATH_ENCODING";return false;}
        const auto raw=QString::fromUtf8(utf8);
        const auto name=QDir::fromNativeSeparators(raw);
        if (!safeArchiveMember(name) || archive_entry_symlink(entry) || archive_entry_hardlink(entry)) {
            error="PROVIDER_ARCHIVE_UNSAFE_PATH";return false;
        }
        const auto type=archive_entry_filetype(entry);
        const auto target=root+'/'+name;
        if (type==AE_IFDIR) {
            if (!QDir().mkpath(target)) {error="PROVIDER_STAGING_CREATE_FAILED";return false;}
            continue;
        }
        if (type!=AE_IFREG || names.contains(name.toCaseFolded()) || names.size()>=8192) {
            error="PROVIDER_ARCHIVE_UNSAFE_ENTRY";return false;
        }
        names.insert(name.toCaseFolded());
        const auto size=archive_entry_size(entry);
        if (size<0 || static_cast<std::uint64_t>(size)>maximumExtracted-extracted) {
            error="PROVIDER_ARCHIVE_SIZE_LIMIT";return false;
        }
        if (!QDir().mkpath(QFileInfo(target).absolutePath())) {error="PROVIDER_STAGING_CREATE_FAILED";return false;}
        QSaveFile file(target);file.setDirectWriteFallback(false);
        if (!file.open(QIODevice::WriteOnly)) {error="PROVIDER_STAGING_WRITE_FAILED";return false;}
        char block[64*1024];la_ssize_t received;std::uint64_t written=0;
        while ((received=archive_read_data(reader.get(),block,sizeof block))>0) {
            if (QThread::currentThread()->isInterruptionRequested()) {error="PROVIDER_INSTALL_CANCELLED";return false;}
            written+=received;
            if (written>static_cast<std::uint64_t>(size) || file.write(block,received)!=received) {
                error="PROVIDER_STAGING_WRITE_FAILED";return false;
            }
        }
        if (received<0 || written!=static_cast<std::uint64_t>(size) || !file.commit()) {
            error="PROVIDER_ARCHIVE_INCOMPLETE";return false;
        }
        extracted+=written;
    }
    if (status!=ARCHIVE_EOF) {error="PROVIDER_ARCHIVE_INCOMPLETE";return false;}
    return true;
}

} // namespace

bool ProviderStore::downloadDirectMl(const std::filesystem::path& destination,
                                    ProviderDownloadProgress progress, std::string& error,
                                    ProviderStagingValidator validate)
{
    if (destination.filename() != "dml") { error = "unsafe DirectML destination"; return false; }
    const auto parent = QString::fromStdWString(destination.parent_path().wstring());
    if (!QDir().mkpath(parent)) { error = "cannot create provider storage"; return false; }
    QTemporaryDir temp(parent + QStringLiteral("/dml-download-XXXXXX"));
    if (!temp.isValid()) { error = "cannot create provider staging"; return false; }
    const auto root = std::filesystem::path(temp.path().toStdWString());
    const ModelAsset packages[] = {
        {"ort.nupkg", "57e9f11b73437bef7a309496135d4c1f96b1a8e9ddba60013fa27bfc1d788681", 12458649,
         "https://api.nuget.org/v3-flatcontainer/microsoft.ml.onnxruntime.directml/1.24.4/microsoft.ml.onnxruntime.directml.1.24.4.nupkg"},
        {"directml.nupkg", "4e7cb7ddce8cf837a7a75dc029209b520ca0101470fcdf275c1f49736a3615b9", 202292617,
         "https://api.nuget.org/v3-flatcontainer/microsoft.ai.directml/1.15.4/microsoft.ai.directml.1.15.4.nupkg"}
    };
    std::uint64_t completed = 0;
    for (const auto& package : packages) {
        const auto archive=destination.parent_path()/"installer-cache"/"dml"/package.sha256/package.filename;
        if (!ModelStore::download(package, archive, [&](std::uint64_t bytes, std::uint64_t) {
                if (progress) progress(completed + bytes, 214751266);
            }, error)) return false;
        if (!extractArchive(archive, root / (package.filename + ".contents"), error)) return false;
        completed += package.sizeBytes;
    }
    // Only the Windows x64 release DLLs are installed, never ARM/x86/debug/Xbox.
    const auto ready = root / "dml";
    std::error_code ec;
    std::filesystem::create_directory(ready, ec);
    const std::pair<std::filesystem::path, const char*> files[] = {
        {root / "ort.nupkg.contents/runtimes/win-x64/native/onnxruntime.dll", "onnxruntime.dll"},
        {root / "ort.nupkg.contents/runtimes/win-x64/native/onnxruntime_providers_shared.dll", "onnxruntime_providers_shared.dll"},
        {root / "directml.nupkg.contents/bin/x64-win/DirectML.dll", "DirectML.dll"},
        {root / "ort.nupkg.contents/LICENSE", "ONNX-Runtime-LICENSE.txt"},
        {root / "directml.nupkg.contents/LICENSE.txt", "DirectML-LICENSE.txt"}
    };
    for (const auto& [source, name] : files) {
        std::filesystem::copy_file(source, ready / name, ec);
        if (ec) { error = "prepare DirectML: " + ec.message(); return false; }
    }
    if (validate && !validate(ready,error)) return false;
    if(QThread::currentThread()->isInterruptionRequested()) {error="PROVIDER_INSTALL_CANCELLED";return false;}
    // Do not overwrite a runtime potentially loaded in this process.
    if (std::filesystem::exists(destination, ec)) {
        error = "DirectML directory already exists; restart the application before retrying";
        return false;
    }
    std::filesystem::rename(ready, destination, ec);
    if (ec) { error = "install DirectML: " + ec.message(); return false; }
    return true;
}

std::optional<ProviderAsset> ProviderStore::fetchManifest(const std::string& url,
                                                          const std::string& provider,
                                                          std::string& error)
{
    if (!safeProvider(provider)) {
        error = "unsupported provider";
        return std::nullopt;
    }
    const QUrl requestUrl(QString::fromStdString(url));
    if (!requestUrl.isValid()
        || requestUrl.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0
        || !trustedHost(requestUrl)) {
        error = "provider manifest requires an HTTPS GitHub URL";
        return std::nullopt;
    }
    QNetworkAccessManager manager;
    QNetworkRequest request(requestUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("ParallelFinder/0.1"));
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::UserVerifiedRedirectPolicy);
    request.setTransferTimeout(30000);
    QNetworkReply* reply = manager.get(request);
    QTimer interruption;
    detail::watchInterruption(interruption, *reply, error);
    detail::boundManifestReply(*reply, kMaxManifestBytes, error, "provider manifest is too large");
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::redirected, [&](const QUrl& target) {
        if (target.scheme() == QStringLiteral("https") && trustedHost(target))
            reply->redirectAllowed();
        else {
            error = "provider manifest redirected to an untrusted URL";
            reply->abort();
        }
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, [&] {
        error = "provider manifest request timed out";
        reply->abort();
    });
    timeout.start(90000);
    loop.exec();
    const auto networkError = reply->error();
    const std::string networkMessage = reply->errorString().toStdString();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (networkError != QNetworkReply::NoError) {
        if (error.empty()) error = "provider manifest: " + networkMessage;
        reply->deleteLater();
        return std::nullopt;
    }
    if (!trustedHost(reply->url())) {
        error = "provider manifest redirected to an untrusted host";
        reply->deleteLater();
        return std::nullopt;
    }
    if (reply->bytesAvailable() > kMaxManifestBytes) {
        error = "provider manifest is too large";
        reply->deleteLater();
        return std::nullopt;
    }
    if (status < 200 || status >= 300) {
        error = "provider manifest: HTTP " + std::to_string(status);
        reply->deleteLater();
        return std::nullopt;
    }
    const auto result = parseManifest(reply->readAll(), provider, error);
    reply->deleteLater();
    return result;
}

std::optional<ProviderAsset> ProviderStore::readManifest(const std::filesystem::path& path,
    const std::string& provider, std::string& error)
{
    QFile file(QString::fromStdWString(path.wstring()));
    if (!file.open(QIODevice::ReadOnly) || file.size() > kMaxManifestBytes) {
        error = "cannot read provider manifest"; return std::nullopt;
    }
    return parseManifest(file.readAll(), provider, error);
}

bool ProviderStore::assembleParts(const ProviderAsset& asset,
    const std::vector<std::filesystem::path>& paths,
    const std::filesystem::path& destination, std::string& error)
{
    if (asset.parts.empty() || paths.size() != asset.parts.size()) { error = "missing provider parts"; return false; }
    QSaveFile output(QString::fromStdWString(destination.wstring()));
    if (!output.open(QIODevice::WriteOnly)) { error = output.errorString().toStdString(); return false; }
    std::uint64_t written = 0;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (std::size_t i = 0; i < paths.size(); ++i) {
        QFile input(QString::fromStdWString(paths[i].wstring()));
        if (!input.open(QIODevice::ReadOnly) || static_cast<std::uint64_t>(input.size()) != asset.parts[i].sizeBytes
            || !ModelStore::verifySha256(paths[i], asset.parts[i].sha256, error)) {
            error = "provider part verification failed: " + error; return false;
        }
        while (!input.atEnd()) {
            const auto bytes = input.read(1024*1024);
            if (bytes.isEmpty() || output.write(bytes) != bytes.size()) { error = "provider part read/write failed"; return false; }
            hash.addData(bytes);
            written += static_cast<std::uint64_t>(bytes.size());
        }
    }
    if (written != asset.sizeBytes) { error = "assembled provider size mismatch"; return false; }
    if (hash.result().toHex().toStdString() != asset.sha256) { error = "assembled provider SHA-256 mismatch"; return false; }
    if (!output.commit()) { error = output.errorString().toStdString(); return false; }
    return true;
}

std::filesystem::path ProviderStore::installerCacheDirectory(const ProviderAsset& asset,
    const std::filesystem::path& destination)
{
    const auto safeHash = asset.sha256.size()==64 && std::all_of(asset.sha256.begin(),asset.sha256.end(),
        [](char c) {return (c>='0' && c<='9') || (c>='a' && c<='f') || (c>='A' && c<='F');});
    if (!safeProvider(asset.provider) || !safeHash) return {};
    return destination.parent_path() / "installer-cache" / asset.provider / asset.sha256;
}

bool ProviderStore::downloadAndInstall(const ProviderAsset& asset,
                                       const std::filesystem::path& destination,
                                       ProviderDownloadProgress progress,
                                       std::string& error,
                                       ProviderStagingValidator validate)
{
    if (!safeProvider(asset.provider) || !safeArchive(asset.archive)) {
        error = "provider asset contains unsafe paths";
        return false;
    }
    if (destination.empty() || destination.filename() != std::filesystem::path(asset.provider)) {
        error = "provider destination is unsafe";
        return false;
    }
    for (const auto& part : asset.parts) {
        if (!safeArchive(part.archive) || !part.sizeBytes || part.sha256.size()!=64) {
            error="PROVIDER_PART_METADATA_INVALID";return false;
        }
    }
    ModelAsset archive;
    archive.filename = asset.archive;
    archive.sha256 = asset.sha256;
    archive.sizeBytes = asset.sizeBytes;
    archive.downloadUrl = asset.downloadUrl;
    const auto packageRoot=installerCacheDirectory(asset,destination);
    if (packageRoot.empty() || !asset.sizeBytes || asset.sizeBytes>8ULL*1024*1024*1024) {
        error="PROVIDER_PACKAGE_METADATA_INVALID"; return false;
    }
    if (!QDir().mkpath(QString::fromStdWString(packageRoot.wstring()))) {
        error="PROVIDER_CACHE_CREATE_FAILED"; return false;
    }
    QLockFile installLock(QString::fromStdWString((destination.parent_path()/(asset.provider+".install.lock")).wstring()));
    if (!installLock.tryLock(0)) {error="PROVIDER_INSTALL_BUSY";return false;}
    const auto interruptedBackup=destination.parent_path()/(asset.provider+".previous");
    std::error_code recoveryError;
    if (std::filesystem::exists(interruptedBackup,recoveryError) && !std::filesystem::exists(destination,recoveryError)) {
        std::filesystem::rename(interruptedBackup,destination,recoveryError);
        if (recoveryError) {error="PROVIDER_PREVIOUS_RESTORE_FAILED: "+recoveryError.message();return false;}
    }
    const auto temporary = packageRoot / asset.archive;
    if (asset.parts.empty()) {
        if (!detail::downloadWithRetry(archive,temporary,std::move(progress),error,ModelStore::download)) return false;
    } else {
        const auto downloads=packageRoot/"parts";
        if (!QDir().mkpath(QString::fromStdWString(downloads.wstring()))) { error="cannot create provider parts cache";return false; }
        std::vector<std::filesystem::path> paths;
        for(const auto& part:asset.parts)paths.push_back(downloads/part.archive);
        std::string cachedError;std::error_code statError;
        const bool cached=std::filesystem::is_regular_file(temporary,statError)
            && std::filesystem::file_size(temporary,statError)==asset.sizeBytes
            && ModelStore::verifySha256(temporary,asset.sha256,cachedError);
        if(cached) {if(progress)progress(asset.sizeBytes,asset.sizeBytes);}
        else {
            if(!detail::downloadProviderParts(asset,downloads,std::move(progress),error,ModelStore::download))return false;
            if (!assembleParts(asset, paths, temporary, error)) return false;
        }
    }
    // ModelStore installs the downloaded file at `temporary`; keep it out of
    // the runtime directory until extraction has completed successfully.
    QTemporaryDir stagingRoot(QString::fromStdWString(destination.parent_path().wstring())+"/provider-stage-XXXXXX");
    if (!stagingRoot.isValid()) {error="PROVIDER_STAGING_CREATE_FAILED";return false;}
    const auto staging = std::filesystem::path(stagingRoot.path().toStdWString()) / asset.provider;
    // A manifest is informational only: reuse always rechecks size and SHA-256.
    QSaveFile manifest(QString::fromStdWString((packageRoot/"verified.json").wstring()));
    if (manifest.open(QIODevice::WriteOnly)) {
        const auto bytes=QJsonDocument(QJsonObject{{"schema",1},{"provider",QString::fromStdString(asset.provider)},
            {"sha256",QString::fromStdString(asset.sha256)},{"sizeBytes",static_cast<qint64>(asset.sizeBytes)},
            {"verified",true}}).toJson(QJsonDocument::Compact);
        if (manifest.write(bytes)==bytes.size()) manifest.commit();
    }
    std::error_code stagingError;
    std::filesystem::remove_all(staging, stagingError);
    if (stagingError) {
        error = "clear provider staging directory: " + stagingError.message();

        return false;
    }
    if (!extractArchive(temporary, staging, error)) {
        std::error_code ignored;

        std::filesystem::remove_all(staging, ignored);
        return false;
    }
    if (!validateExtractedTree(staging, error)) {
        std::error_code ignored;

        std::filesystem::remove_all(staging, ignored);
        return false;
    }
    if(QThread::currentThread()->isInterruptionRequested()) {error="PROVIDER_INSTALL_CANCELLED";return false;}
    // Do not replace a live provider directory until the complete archive is
    // extracted.  Keep a short-lived backup so a failed rename can restore it.
    if (validate && !validate(staging,error)) {
        if (error.empty()) error="PROVIDER_STAGING_PROBE_FAILED";
        return false;
    }
    if(QThread::currentThread()->isInterruptionRequested()) {error="PROVIDER_INSTALL_CANCELLED";return false;}
    const auto backup = destination.parent_path() / (asset.provider + ".previous");
    std::error_code installError;
    if (std::filesystem::exists(backup,installError) && !std::filesystem::exists(destination,installError)) {
        std::filesystem::rename(backup,destination,installError);
        if (installError) {error="PROVIDER_PREVIOUS_RESTORE_FAILED: "+installError.message();return false;}
    }
    std::filesystem::remove_all(backup, installError);
    if (installError) {
        error = "clear previous provider backup: " + installError.message();

        std::filesystem::remove_all(staging, installError);
        return false;
    }
    if (std::filesystem::exists(destination, installError)) {
        std::filesystem::rename(destination, backup, installError);
        if (installError) {
            error = "stage current provider runtime: " + installError.message();

            std::filesystem::remove_all(staging, installError);
            return false;
        }
    }
    std::filesystem::rename(staging, destination, installError);
    if (installError) {
        std::error_code restoreError;
        if (std::filesystem::exists(backup, restoreError))
            std::filesystem::rename(backup, destination, restoreError);
        error = "install provider runtime: " + installError.message();

        std::filesystem::remove_all(staging, restoreError);
        return false;
    }
    std::error_code ignored;
    std::filesystem::remove_all(backup, ignored);

    return true;
}

} // namespace pfservices
