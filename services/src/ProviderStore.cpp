#include <pfservices/ProviderStore.hpp>

#include <QEventLoop>
#include <QFile>
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

bool safeProvider(const std::string& provider)
{
    return provider == "dml" || provider == "cuda" || provider == "tensorrt" || provider == "cpu";
}

bool safeArchive(const std::string& archive)
{
    const std::filesystem::path path(archive);
    return !archive.empty() && path.filename() == path && archive.find("..") == std::string::npos;
}

std::optional<ProviderAsset> parseManifest(const QByteArray& bytes,
                                           const std::string& provider,
                                           std::string& error)
{
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
    std::error_code filesystemError;
    std::filesystem::create_directories(destination, filesystemError);
    if (filesystemError) {
        error = "create provider directory: " + filesystemError.message();
        return false;
    }
    QProcess process;
    process.setProgram(QStringLiteral("tar.exe"));
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
        || requestUrl.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0) {
        error = "provider manifest requires an HTTPS URL";
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
    ModelAsset archive;
    archive.filename = asset.archive;
    archive.sha256 = asset.sha256;
    archive.sizeBytes = asset.sizeBytes;
    archive.downloadUrl = asset.downloadUrl;
    const auto temporary = destination / (asset.archive + ".part");
    if (!ModelStore::download(archive, temporary, std::move(progress), error)) return false;
    // ModelStore installs the downloaded file at `temporary`; keep it out of
    // the runtime directory until extraction has completed successfully.
    if (!extractArchive(temporary, destination, error)) {
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        return false;
    }
    std::error_code ignored;
    std::filesystem::remove(temporary, ignored);
    return true;
}

} // namespace pfservices
