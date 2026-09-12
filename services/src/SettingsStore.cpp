#include <pfservices/SettingsStore.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <filesystem>
#include <utility>

namespace pfservices {
namespace {

QJsonObject toJson(const Settings& settings)
{
    QJsonObject json;
    json[QStringLiteral("schemaVersion")] = 3;
    json[QStringLiteral("provider")] = QString::fromStdString(settings.provider);
    json[QStringLiteral("language")] = QString::fromStdString(settings.language);
    json[QStringLiteral("theme")] = QString::fromStdString(settings.theme);
    json[QStringLiteral("modelPath")] = QString::fromStdString(settings.modelPath);
    json[QStringLiteral("cachePath")] = QString::fromStdString(settings.cachePath);
    json[QStringLiteral("cacheLimitBytes")] = static_cast<qint64>(settings.cacheLimitBytes);
    json[QStringLiteral("sceneThreshold")] = settings.sceneThreshold;
    json[QStringLiteral("sceneMinFrames")] = static_cast<qint64>(settings.sceneMinFrames);
    json[QStringLiteral("sceneAdaptiveMultiplier")] = settings.sceneAdaptiveMultiplier;
    json[QStringLiteral("similarityThreshold")] = settings.similarityThreshold;
    json[QStringLiteral("candidateThreshold")] = settings.candidateThreshold;
    json[QStringLiteral("minRepeatGapSec")] = settings.minRepeatGapSec;
    json[QStringLiteral("sameFileGapSec")] = settings.sameFileGapSec;
    json[QStringLiteral("crossFileGapSec")] = settings.crossFileGapSec;
    json[QStringLiteral("duplicateWindowSec")] = settings.duplicateWindowSec;
    json[QStringLiteral("noiseFactor")] = settings.noiseFactor;
    json[QStringLiteral("maxUniqueResults")] = static_cast<qint64>(settings.maxUniqueResults);
    json[QStringLiteral("timeWeight")] = settings.timeWeight;
    json[QStringLiteral("sakoeChibaRatio")] = settings.sakoeChibaRatio;
    return json;
}

void readString(const QJsonObject& json, const char* key, std::string& destination)
{
    const auto value = json.value(QLatin1String(key));
    if (value.isString()) destination = value.toString().toStdString();
}

} // namespace

std::string SettingsStore::defaultDirectory()
{
    const QString location = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    return location.isEmpty() ? std::string(".") : location.toStdString();
}

std::string SettingsStore::defaultPath()
{
    return (std::filesystem::path(defaultDirectory()) / "settings.json").string();
}

SettingsStore::SettingsStore(std::string path)
    : path_(path.empty() ? defaultPath() : std::move(path))
{
}

Settings SettingsStore::load(std::string& error) const
{
    Settings settings;
    QFile file(QString::fromStdString(path_));
    if (!file.exists()) return settings;
    if (!file.open(QIODevice::ReadOnly)) {
        error = "open settings: " + file.errorString().toStdString();
        return settings;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        error = "parse settings: " + parseError.errorString().toStdString();
        return settings;
    }
    const QJsonObject json = document.object();
    const auto schemaVersion = json.value(QStringLiteral("schemaVersion"));
    const bool legacySceneScale = !schemaVersion.isDouble() || schemaVersion.toInteger() < 2;
    readString(json, "provider", settings.provider);
    readString(json, "language", settings.language);
    readString(json, "theme", settings.theme);
    readString(json, "modelPath", settings.modelPath);
    readString(json, "cachePath", settings.cachePath);
    const auto cacheLimit = json.value(QStringLiteral("cacheLimitBytes"));
    if (cacheLimit.isDouble() && cacheLimit.toInteger() > 0)
        settings.cacheLimitBytes = static_cast<std::size_t>(cacheLimit.toInteger());
    const auto sceneThreshold = json.value(QStringLiteral("sceneThreshold"));
    if (sceneThreshold.isDouble() && sceneThreshold.toDouble() > 0.0) {
        settings.sceneThreshold = sceneThreshold.toDouble();
        // Stage-2 originally stored a normalized histogram threshold (0..1).
        // The HSV detector now uses the documented 0..255 content scale.
        if (legacySceneScale && settings.sceneThreshold <= 1.0)
            settings.sceneThreshold = 27.0;
    }
    const auto minFrames = json.value(QStringLiteral("sceneMinFrames"));
    if (minFrames.isDouble() && minFrames.toInteger() > 0)
        settings.sceneMinFrames = static_cast<std::size_t>(minFrames.toInteger());
    const auto adaptive = json.value(QStringLiteral("sceneAdaptiveMultiplier"));
    if (adaptive.isDouble() && adaptive.toDouble() >= 0.0)
        settings.sceneAdaptiveMultiplier = adaptive.toDouble();
    const auto readDouble = [&](const char* key, double& target, double minimum, double maximum) {
        const auto value = json.value(QLatin1String(key));
        if (value.isDouble() && value.toDouble() >= minimum && value.toDouble() <= maximum)
            target = value.toDouble();
    };
    readDouble("similarityThreshold", settings.similarityThreshold, 0.0, 1.0);
    readDouble("candidateThreshold", settings.candidateThreshold, 0.0, 1.0);
    readDouble("minRepeatGapSec", settings.minRepeatGapSec, 0.0, 60.0);
    readDouble("sameFileGapSec", settings.sameFileGapSec, 0.0, 30.0);
    readDouble("crossFileGapSec", settings.crossFileGapSec, 0.0, 30.0);
    readDouble("duplicateWindowSec", settings.duplicateWindowSec, 0.0, 10.0);
    readDouble("noiseFactor", settings.noiseFactor, 0.0, 3.0);
    readDouble("timeWeight", settings.timeWeight, 0.0, 1.0);
    readDouble("sakoeChibaRatio", settings.sakoeChibaRatio, 0.0, 1.0);
    const auto maxUnique = json.value(QStringLiteral("maxUniqueResults"));
    if (maxUnique.isDouble() && maxUnique.toInteger() > 0 && maxUnique.toInteger() <= 1000)
        settings.maxUniqueResults = static_cast<std::size_t>(maxUnique.toInteger());
    return settings;
}

bool SettingsStore::save(const Settings& settings, std::string& error) const
{
    const QFileInfo info(QString::fromStdString(path_));
    if (!QDir().mkpath(info.absolutePath())) {
        error = "create settings directory failed: " + info.absolutePath().toStdString();
        return false;
    }
    QSaveFile file(QString::fromStdString(path_));
    if (!file.open(QIODevice::WriteOnly)) {
        error = "open settings for write: " + file.errorString().toStdString();
        return false;
    }
    const QByteArray data = QJsonDocument(toJson(settings)).toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size()) {
        error = "write settings: " + file.errorString().toStdString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        error = "commit settings: " + file.errorString().toStdString();
        return false;
    }
    return true;
}

} // namespace pfservices
