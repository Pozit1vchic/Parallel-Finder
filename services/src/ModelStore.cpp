#include <pfservices/ModelStore.hpp>

#include <QCryptographicHash>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <cstdlib>
#include <sstream>
#include <utility>
#include <vector>

#include <pfservices/SettingsStore.hpp>

namespace pfservices {
namespace {

std::vector<std::filesystem::path> candidateRoots(const std::filesystem::path& executableDirectory)
{
    std::vector<std::filesystem::path> roots;
    roots.push_back(executableDirectory / "models");
    roots.push_back(std::filesystem::path(SettingsStore::defaultDirectory()) / "models");
    if (const char* root = std::getenv("PF_MODEL_ROOT"); root && *root)
        roots.emplace_back(root);
    return roots;
}

std::optional<ModelAsset> assetFromObject(const QJsonObject& object,
                                          const std::string& requestedFilename)
{
    const auto filename = object.value(QStringLiteral("filename"));
    if (!filename.isString() || filename.toString().toStdString() != requestedFilename)
        return std::nullopt;
    ModelAsset asset;
    asset.filename = requestedFilename;
    const auto sha = object.value(QStringLiteral("sha256"));
    if (sha.isString()) asset.sha256 = sha.toString().toStdString();
    const auto size = object.value(QStringLiteral("sizeBytes"));
    if (size.isDouble() && size.toInteger() > 0) asset.sizeBytes = static_cast<std::uint64_t>(size.toInteger());
    const auto url = object.value(QStringLiteral("url"));
    if (url.isString()) asset.downloadUrl = url.toString().toStdString();
    const auto license = object.value(QStringLiteral("license"));
    if (license.isString()) asset.license = license.toString().toStdString();
    const auto minimum = object.value(QStringLiteral("minimumAppVersion"));
    if (minimum.isString()) asset.minimumAppVersion = minimum.toString().toStdString();
    return asset;
}

} // namespace

bool ModelStore::verifySha256(const std::filesystem::path& path,
                              const std::string& expected,
                              std::string& error)
{
    if (expected.empty()) return true;
    QFile file(QString::fromStdWString(path.wstring()));
    if (!file.open(QIODevice::ReadOnly)) {
        error = "open model for hash: " + file.errorString().toStdString();
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray chunk = file.read(1024 * 1024);
        if (chunk.isEmpty() && !file.atEnd()) {
            error = "read model while hashing: " + file.errorString().toStdString();
            return false;
        }
        hash.addData(chunk);
    }
    const std::string actual = hash.result().toHex().toStdString();
    if (actual != expected) {
        error = "model SHA-256 mismatch: expected " + expected + ", got " + actual;
        return false;
    }
    return true;
}

std::optional<std::filesystem::path> ModelStore::resolve(
    const ModelAsset& asset,
    const std::filesystem::path& executableDirectory,
    std::string& error)
{
    std::ostringstream checked;
    for (const auto& root : candidateRoots(executableDirectory)) {
        const auto candidate = root / asset.filename;
        std::error_code filesystemError;
        if (!std::filesystem::is_regular_file(candidate, filesystemError)) {
            checked << candidate.string() << "; ";
            continue;
        }
        std::string hashError;
        if (!verifySha256(candidate, asset.sha256, hashError)) {
            error = hashError;
            return std::nullopt;
        }
        if (asset.sizeBytes != 0 && std::filesystem::file_size(candidate, filesystemError)
            != asset.sizeBytes) {
            error = "model size mismatch: " + candidate.string();
            return std::nullopt;
        }
        return candidate;
    }
    error = "model '" + asset.filename + "' was not found; checked: " + checked.str();
    return std::nullopt;
}

std::optional<ModelAsset> ModelStore::readManifest(const std::filesystem::path& path,
                                                   const std::string& filename,
                                                   std::string& error)
{
    QFile file(QString::fromStdWString(path.wstring()));
    if (!file.open(QIODevice::ReadOnly)) {
        error = "open model manifest: " + file.errorString().toStdString();
        return std::nullopt;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        error = "parse model manifest: " + parseError.errorString().toStdString();
        return std::nullopt;
    }
    const QJsonObject root = document.object();
    const QJsonValue modelsValue = root.value(QStringLiteral("models"));
    if (modelsValue.isArray()) {
        for (const auto& item : modelsValue.toArray()) {
            if (item.isObject()) {
                if (auto asset = assetFromObject(item.toObject(), filename)) return asset;
            }
        }
    } else if (modelsValue.isObject()) {
        const auto object = modelsValue.toObject().value(QString::fromStdString(filename));
        if (object.isObject()) {
            QJsonObject normalized = object.toObject();
            normalized[QStringLiteral("filename")] = QString::fromStdString(filename);
            if (auto asset = assetFromObject(normalized, filename)) return asset;
        }
    }
    error = "model '" + filename + "' is absent from manifest";
    return std::nullopt;
}

bool ModelStore::download(const ModelAsset& asset,
                          const std::filesystem::path& destination,
                          DownloadProgress progress,
                          std::string& error)
{
    if (asset.downloadUrl.empty()) {
        error = "model download URL is empty";
        return false;
    }
    const QUrl url(QString::fromStdString(asset.downloadUrl));
    if (!url.isValid() || url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) != 0) {
        error = "model download requires an HTTPS URL";
        return false;
    }

    std::error_code filesystemError;
    const auto parent = destination.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, filesystemError);
        if (filesystemError) {
            error = "create model directory: " + filesystemError.message();
            return false;
        }
    }
    if (std::filesystem::is_regular_file(destination, filesystemError)) {
        if ((asset.sizeBytes == 0 || std::filesystem::file_size(destination, filesystemError) == asset.sizeBytes)
            && verifySha256(destination, asset.sha256, error)) return true;
    }

    const auto partPath = destination.string() + ".part";
    QFile part(QString::fromStdString(partPath));
    qint64 offset = 0;
    if (part.exists()) {
        offset = part.size();
        if (!part.open(QIODevice::ReadWrite | QIODevice::Append)) {
            error = "open partial model: " + part.errorString().toStdString();
            return false;
        }
    } else if (!part.open(QIODevice::ReadWrite | QIODevice::Truncate)) {
        error = "create partial model: " + part.errorString().toStdString();
        return false;
    }

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("ParallelFinder/0.1"));
    if (offset > 0) {
        request.setRawHeader("Range", "bytes=" + QByteArray::number(offset) + "-");
    }
    QNetworkAccessManager manager;
    QNetworkReply* reply = manager.get(request);
    QEventLoop loop;
    bool metadataSeen = false;
    qint64 expectedTotal = asset.sizeBytes == 0 ? -1 : static_cast<qint64>(asset.sizeBytes);
    QObject::connect(reply, &QNetworkReply::metaDataChanged, [&] {
        metadataSeen = true;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status != 200 && status != 206) {
            reply->abort();
            return;
        }
        if (offset > 0 && status == 200) {
            part.resize(0);
            offset = 0;
        }
        const qint64 total = reply->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        if (total > 0 && status == 206) expectedTotal = offset + total;
        else if (total > 0) expectedTotal = total;
    });
    QObject::connect(reply, &QNetworkReply::readyRead, [&] {
        const QByteArray data = reply->readAll();
        if (part.write(data) != data.size()) reply->abort();
    });
    QObject::connect(reply, &QNetworkReply::downloadProgress,
                     [&](qint64 received, qint64 total) {
        if (progress) progress(static_cast<std::uint64_t>(offset + received),
                               total > 0 ? static_cast<std::uint64_t>(offset + total)
                                         : static_cast<std::uint64_t>(std::max<qint64>(0, expectedTotal)));
    });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer timeout;
    timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
    timeout.start(120000);
    loop.exec();
    const auto networkError = reply->error();
    const std::string networkMessage = reply->errorString().toStdString();
    if (!metadataSeen && networkError == QNetworkReply::NoError) {
        error = "model download returned no HTTP metadata";
        reply->deleteLater();
        part.close();
        return false;
    }
    if (networkError != QNetworkReply::NoError) {
        error = "download model: " + networkMessage;
        reply->deleteLater();
        part.close();
        return false;
    }
    const QByteArray tail = reply->readAll();
    if (!tail.isEmpty() && part.write(tail) != tail.size()) {
        error = "write partial model: " + part.errorString().toStdString();
        reply->deleteLater();
        part.close();
        return false;
    }
    reply->deleteLater();
    part.flush();
    part.close();

    const auto actualSize = std::filesystem::file_size(partPath, filesystemError);
    if (filesystemError || (asset.sizeBytes != 0 && actualSize != asset.sizeBytes)) {
        error = "downloaded model size mismatch";
        return false;
    }
    if (!verifySha256(partPath, asset.sha256, error)) return false;
    std::filesystem::remove(destination, filesystemError);
    filesystemError.clear();
    std::filesystem::rename(partPath, destination, filesystemError);
    if (filesystemError) {
        error = "install downloaded model: " + filesystemError.message();
        return false;
    }
    return true;
}

} // namespace pfservices
