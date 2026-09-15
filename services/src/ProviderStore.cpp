#include <pfservices/ProviderStore.hpp>

#include <QEventLoop>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QTimer>
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

QString tarProgram()
{
#if defined(_WIN32)
    const QString windir = qEnvironmentVariable("WINDIR");
    if (!windir.isEmpty()) {
        const QString systemTar = QDir(windir).filePath(QStringLiteral("System32/tar.exe"));
        if (QFileInfo(systemTar).isFile()) return systemTar;
    }
#endif
    return QStringLiteral("tar.exe");
}

bool safeArchiveMember(const QString& rawName)
{
    QString name = QDir::fromNativeSeparators(rawName.trimmed());
    if (name.isEmpty() || name.startsWith(QLatin1Char('/'))
        || (name.size() >= 2 && name.at(1) == QLatin1Char(':')))
        return false;
    const QStringList components = name.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString& component : components) {
        if (component == QStringLiteral("..") || component.contains(QChar(0))) return false;
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

bool runTarListing(const QStringList& arguments,
                   QByteArray& output,
                   std::string& error)
{
    QProcess process;
    process.setProgram(tarProgram());
    process.setArguments(arguments);
#if defined(_WIN32)
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();
    if (!process.waitForStarted(5000) || !process.waitForFinished(30000)) {
        error = "inspect provider archive: " + process.errorString().toStdString();
        process.kill();
        return false;
    }
    output = process.readAllStandardOutput();
    if (output.size() > 32 * 1024 * 1024) {
        error = "provider archive listing is too large";
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QByteArray stderrData = process.readAllStandardError();
        error = "inspect provider archive failed: "
            + (stderrData.isEmpty() ? process.errorString().toStdString() : stderrData.toStdString());
        return false;
    }
    return true;
}

bool validateArchive(const std::filesystem::path& archive, std::string& error)
{
    const QString archiveArgument = QString::fromStdWString(archive.wstring());
    QByteArray names;
    if (!runTarListing({QStringLiteral("-tf"), archiveArgument}, names, error)) return false;
    for (const QByteArray& rawLine : names.split('\n')) {
        const QString line = QString::fromLocal8Bit(rawLine).trimmed();
        if (line.isEmpty()) continue;
        if (!safeArchiveMember(line)) {
            error = "provider archive contains an unsafe path";
            return false;
        }
    }

    // A symlink or hardlink inside the archive can escape the extraction
    // directory even when its displayed name is harmless.  Reject links and
    // special files before tar gets a chance to materialize them.
    QByteArray details;
    if (!runTarListing({QStringLiteral("-tvf"), archiveArgument}, details, error)) return false;
    for (const QByteArray& rawLine : details.split('\n')) {
        const QByteArray line = rawLine.trimmed();
        if (line.isEmpty()) continue;
        const char type = line.front();
        if (type == 'l' || type == 'h' || type == 'c' || type == 'b'
            || type == 'p' || type == 's') {
            error = "provider archive contains a link or special file";
            return false;
        }
    }
    return true;
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
        if (!safeArchive(asset.archive) || asset.downloadUrl.empty()) {
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
    if (!validateArchive(archive, error)) return false;
    std::error_code filesystemError;
    std::filesystem::create_directories(destination, filesystemError);
    if (filesystemError) {
        error = "create provider directory: " + filesystemError.message();
        return false;
    }
    QProcess process;
    process.setProgram(tarProgram());
    process.setArguments({QStringLiteral("-xf"),
                          QString::fromStdWString(archive.wstring()),
                          QStringLiteral("-C"),
                          QString::fromStdWString(destination.wstring())});
#if defined(_WIN32)
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();
    if (!process.waitForStarted(5000) || !process.waitForFinished(120000)) {
        error = "extract provider archive: " + process.errorString().toStdString();
        process.kill();
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QByteArray stderrData = process.readAllStandardError();
        error = "extract provider archive failed: "
            + (stderrData.isEmpty() ? process.errorString().toStdString() : stderrData.toStdString());
        return false;
    }
    return true;
}

} // namespace

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
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = manager.get(request);
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
    timeout.start(15000);
    loop.exec();
    const auto networkError = reply->error();
    const std::string networkMessage = reply->errorString().toStdString();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (networkError != QNetworkReply::NoError) {
        error = "provider manifest: " + networkMessage;
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

bool ProviderStore::downloadAndInstall(const ProviderAsset& asset,
                                       const std::filesystem::path& destination,
                                       ProviderDownloadProgress progress,
                                       std::string& error)
{
    if (!safeProvider(asset.provider) || !safeArchive(asset.archive)) {
        error = "provider asset contains unsafe paths";
        return false;
    }
    if (destination.empty() || destination.filename().string() != asset.provider) {
        error = "provider destination is unsafe";
        return false;
    }
    ModelAsset archive;
    archive.filename = asset.archive;
    archive.sha256 = asset.sha256;
    archive.sizeBytes = asset.sizeBytes;
    archive.downloadUrl = asset.downloadUrl;
    const auto temporary = destination / (asset.archive + ".part");
    if (!ModelStore::download(archive, temporary, std::move(progress), error)) return false;
    // ModelStore installs the downloaded file at `temporary`; keep it out of
    // the runtime directory until extraction has completed successfully.
    const auto staging = destination.parent_path()
        / (destination.filename().string() + ".staging");
    std::error_code stagingError;
    std::filesystem::remove_all(staging, stagingError);
    if (stagingError) {
        error = "clear provider staging directory: " + stagingError.message();
        std::filesystem::remove(temporary, stagingError);
        return false;
    }
    if (!extractArchive(temporary, staging, error)) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        std::filesystem::remove_all(staging, ignored);
        return false;
    }
    if (!validateExtractedTree(staging, error)) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        std::filesystem::remove_all(staging, ignored);
        return false;
    }
    // Do not replace a live provider directory until the complete archive is
    // extracted.  Keep a short-lived backup so a failed rename can restore it.
    const auto backup = destination.parent_path()
        / (destination.filename().string() + ".previous");
    std::error_code installError;
    std::filesystem::remove_all(backup, installError);
    if (installError) {
        error = "clear previous provider backup: " + installError.message();
        std::filesystem::remove(temporary, installError);
        std::filesystem::remove_all(staging, installError);
        return false;
    }
    if (std::filesystem::exists(destination, installError)) {
        std::filesystem::rename(destination, backup, installError);
        if (installError) {
            error = "stage current provider runtime: " + installError.message();
            std::filesystem::remove(temporary, installError);
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
        std::filesystem::remove(temporary, restoreError);
        std::filesystem::remove_all(staging, restoreError);
        return false;
    }
    std::error_code ignored;
    std::filesystem::remove_all(backup, ignored);
    std::filesystem::remove(temporary, ignored);
    return true;
}

} // namespace pfservices
