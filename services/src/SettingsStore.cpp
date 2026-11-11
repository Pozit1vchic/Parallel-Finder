#include <pfcore/PathEncoding.hpp>
#include <pfservices/SettingsStore.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

#include <filesystem>
#include <algorithm>
#include <utility>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <array>
#include <shared_mutex>

namespace pfservices {
namespace {

auto& settingsIoLocks() {
    static std::array<std::shared_mutex,64> locks;
    return locks;
}

class SettingsWriter {
public:
    struct Job {
        std::string path, section;
        std::function<void(Settings&)> change;
        std::function<void(std::string)> completion;
    };
    SettingsWriter() : worker_([this] { run(); }) {}
    ~SettingsWriter() {
        { std::lock_guard lock(mutex_); stopping_ = true; }
        ready_.notify_one();
        worker_.join();
    }
    void enqueue(Job job) {
        {
            std::lock_guard lock(mutex_);
            // Keep the newest slider value, while preserving the ordering of
            // changes from different sections. In-flight commits stay intact.
            std::erase_if(jobs_, [&](const Job& old) {
                return old.path == job.path && old.section == job.section;
            });
            jobs_.push_back(std::move(job));
        }
        ready_.notify_one();
    }
    void flush() {
        std::unique_lock lock(mutex_);
        drained_.wait(lock, [this] { return jobs_.empty() && !writing_; });
    }
private:
    void run() {
        for (;;) {
            Job job;
            {
                std::unique_lock lock(mutex_);
                ready_.wait(lock, [this] { return stopping_ || !jobs_.empty(); });
                if (jobs_.empty() && stopping_) return;
                job = std::move(jobs_.front()); jobs_.pop_front(); writing_ = true;
            }
            std::string error;
            try {
                SettingsStore store(job.path);
                auto settings = store.load(error);
                // A failed read is not an empty first-run profile. Applying a
                // partial update to defaults would erase unrelated preferences.
                if(error.empty()) {
                    job.change(settings);
                    store.save(settings,error);
                }
            } catch (const std::exception& exception) { error = exception.what(); }
            catch (...) { error = "settings update failed with an unknown exception"; }
            // A consumer callback must not terminate the writer or strand flush().
            try { if (job.completion) job.completion(std::move(error)); }
            catch (...) { /* The commit is complete; continue draining pending writes. */ }
            {
                std::lock_guard lock(mutex_); writing_ = false;
                if (jobs_.empty()) drained_.notify_all();
            }
        }
    }
    std::mutex mutex_;
    std::condition_variable ready_, drained_;
    std::deque<Job> jobs_;
    bool writing_ = false, stopping_ = false;
    std::jthread worker_;
};

SettingsWriter& settingsWriter() {
    // The writer joins and drains before its I/O locks are destroyed, even
    // when flushPendingWrites() is the first settings operation in a process.
    (void)settingsIoLocks();
    static SettingsWriter writer;return writer;
}

constexpr std::size_t kMaxCacheLimitBytes = 64ULL * 1024ULL * 1024ULL * 1024ULL;
constexpr qsizetype kMaxSettingsBytes = 1 * 1024 * 1024;

// Bounded lock striping avoids retaining one mutex for every settings path.
// File handles must close before an atomic Windows replacement starts. JSON
// conversion and asynchronous change callbacks stay outside the I/O lock.
std::shared_mutex& settingsIoMutex(const std::string& path) {
    auto& locks=settingsIoLocks();
    auto normalized=QDir::cleanPath(QFileInfo(QString::fromStdString(path)).absoluteFilePath());
#ifdef Q_OS_WIN
    normalized=normalized.toCaseFolded();
#endif
    return locks[qHash(normalized)%locks.size()];
}

QJsonObject toJson(const Settings& settings)
{
    QJsonObject json;
    json[QStringLiteral("schemaVersion")] = 6;
    json[QStringLiteral("provider")] = QString::fromStdString(settings.provider);
    json[QStringLiteral("language")] = QString::fromStdString(settings.language);
    json[QStringLiteral("appearance")] = settings.appearance;
    json[QStringLiteral("theme")] = QString::fromStdString(settings.theme);
    json[QStringLiteral("modelPath")] = QString::fromStdString(settings.modelPath);
    json[QStringLiteral("modelChoice")] = QString::fromStdString(settings.modelChoice);
    json[QStringLiteral("cacheEnabled")] = settings.cacheEnabled;
    json[QStringLiteral("cachePath")] = QString::fromStdString(settings.cachePath);
    json[QStringLiteral("cacheLimitBytes")] = static_cast<qint64>(
        std::min(settings.cacheLimitBytes, kMaxCacheLimitBytes));
    json[QStringLiteral("processingThreads")] = static_cast<qint64>(settings.processingThreads);
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
    json[QStringLiteral("qualityProfile")] = QString::fromStdString(settings.qualityProfile);
    json[QStringLiteral("analysisMode")] = QString::fromStdString(settings.analysisMode);
    json[QStringLiteral("normalizeSize")] = settings.normalizeSize;
    json[QStringLiteral("mirrorPoses")] = settings.mirrorPoses;
    json[QStringLiteral("expandedSearch")] = settings.expandedSearch;
    json[QStringLiteral("mcpEnabled")] = settings.mcpEnabled;
    json[QStringLiteral("mcpAllowControl")] = settings.mcpAllowControl;
    json[QStringLiteral("mcpPort")] = settings.mcpPort;
    json[QStringLiteral("mcpToken")] = QString::fromStdString(settings.mcpToken);
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
    return pfcore::pathToUtf8(pfcore::pathFromUtf8(defaultDirectory()) / "settings.json");
}

SettingsStore::SettingsStore(std::string path)
    : path_(path.empty() ? defaultPath() : std::move(path))
{
}

Settings SettingsStore::load(std::string& error) const
{
    error.clear();
    Settings settings;
    QByteArray bytes;
    {
        std::shared_lock lock(settingsIoMutex(path_));
        QFile file(QString::fromStdString(path_));
        if(!file.exists())return settings;
        if(!file.open(QIODevice::ReadOnly)) {
            error="open settings: "+file.errorString().toStdString();return settings;
        }
        if(file.size()<0 || file.size()>kMaxSettingsBytes) {
            error="settings file is too large";return settings;
        }
        bytes=file.read(kMaxSettingsBytes+1);
        if(file.error()!=QFileDevice::NoError || bytes.size()>kMaxSettingsBytes) {
            error="read settings: "+(bytes.size()>kMaxSettingsBytes?std::string("settings file is too large"):file.errorString().toStdString());
            return settings;
        }
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        error = "parse settings: " + parseError.errorString().toStdString();
        return settings;
    }
    const QJsonObject json = document.object();
    const auto schemaVersion = json.value(QStringLiteral("schemaVersion"));
    const bool legacySceneScale = !schemaVersion.isDouble() || schemaVersion.toInteger() < 2;
    readString(json, "provider", settings.provider);
    readString(json, "language", settings.language);
    if (settings.language != "ru" && settings.language != "en" && settings.language != "ky"
        && settings.language != "kk" && settings.language != "uk" && settings.language != "tr") settings.language = "en";
    settings.appearance = json.value(QStringLiteral("appearance")).toObject();
    readString(json, "theme", settings.theme);
    readString(json, "modelPath", settings.modelPath);
    readString(json, "modelChoice", settings.modelChoice);
    readString(json, "cachePath", settings.cachePath);
    if (json[QStringLiteral("cacheEnabled")].isBool())
        settings.cacheEnabled=json[QStringLiteral("cacheEnabled")].toBool();
    // Older folder pickers persisted a file URL instead of a filesystem path.
    // Decode URLs only: percent sequences are legal in ordinary folder names.
    const QUrl cacheUrl(QString::fromStdString(settings.cachePath));
    if (cacheUrl.isLocalFile()) settings.cachePath = cacheUrl.toLocalFile().toStdString();
    readString(json, "qualityProfile", settings.qualityProfile);
    readString(json, "analysisMode", settings.analysisMode);
    if (settings.analysisMode != "motion" && settings.analysisMode != "static"
        && settings.analysisMode != "combined")
        settings.analysisMode = "motion";
    const auto normalizeSize = json.value(QStringLiteral("normalizeSize"));
    if (normalizeSize.isBool()) settings.normalizeSize = normalizeSize.toBool();
    const auto mirrorPoses = json.value(QStringLiteral("mirrorPoses"));
    if (mirrorPoses.isBool()) settings.mirrorPoses = mirrorPoses.toBool();
    const auto expandedSearch = json.value(QStringLiteral("expandedSearch"));
    if (expandedSearch.isBool()) settings.expandedSearch = expandedSearch.toBool();
    if(json["mcpEnabled"].isBool())settings.mcpEnabled=json["mcpEnabled"].toBool();
    if(json["mcpAllowControl"].isBool())settings.mcpAllowControl=json["mcpAllowControl"].toBool();
    if(json["mcpPort"].isDouble() && json["mcpPort"].toDouble()==json["mcpPort"].toInt()
        && json["mcpPort"].toInt()>=1024 && json["mcpPort"].toInt()<=65535)settings.mcpPort=json["mcpPort"].toInt();
    const auto mcpToken=json["mcpToken"].toString();
    if(mcpToken.size()==64 && std::all_of(mcpToken.begin(),mcpToken.end(),[](QChar c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}))
        settings.mcpToken=mcpToken.toStdString();
    const auto cacheLimit = json.value(QStringLiteral("cacheLimitBytes"));
    if (cacheLimit.isDouble() && cacheLimit.toInteger() > 0) {
        const auto value = cacheLimit.toInteger();
        settings.cacheLimitBytes = static_cast<std::size_t>(std::min<qint64>(
            value, static_cast<qint64>(kMaxCacheLimitBytes)));
    }
    const auto processingThreads = json.value(QStringLiteral("processingThreads"));
    if (processingThreads.isDouble() && processingThreads.toInteger() >= 0
        && processingThreads.toInteger() <= 256)
        settings.processingThreads = static_cast<std::size_t>(processingThreads.toInteger());
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
    error.clear();
    const QFileInfo info(QString::fromStdString(path_));
    if (!QDir().mkpath(info.absolutePath())) {
        error = "create settings directory failed: " + info.absolutePath().toStdString();
        return false;
    }
    const QByteArray data = QJsonDocument(toJson(settings)).toJson(QJsonDocument::Indented);
    std::unique_lock lock(settingsIoMutex(path_));
    QSaveFile file(QString::fromStdString(path_));
    if (!file.open(QIODevice::WriteOnly)) {
        error = "open settings for write: " + file.errorString().toStdString();
        return false;
    }
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

void SettingsStore::updateAsync(std::string section, std::function<void(Settings&)> change,
                               std::function<void(std::string)> completion) const
{
    settingsWriter().enqueue({path_, std::move(section), std::move(change), std::move(completion)});
}

void SettingsStore::flushPendingWrites() { settingsWriter().flush(); }

} // namespace pfservices
