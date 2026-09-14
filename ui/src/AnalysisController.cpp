#include "AnalysisController.h"

#include <QMetaObject>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include <QStandardPaths>
#include <QQmlEngine>
#include <QThread>
#include <QVariantMap>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <vector>

#include "pfcore/VideoDecoder.hpp"
#include "pfcore/SceneDetector.hpp"
#include "pfcore/MotionMatcher.hpp"
#include "pfcore/MotionRanker.hpp"
#include "pfcore/DominantPerson.hpp"
#include "pfgpu/PoseEstimator.hpp"
#include "pfgpu/ReIdEstimator.hpp"
#include "pfgpu/DeviceInfo.hpp"
#include "pfservices/SettingsStore.hpp"
#include "pfservices/ModelStore.hpp"
#include "pfservices/PfCache.hpp"
#include "pfexporters/ExportOptions.hpp"

namespace pfui {
namespace {

constexpr auto kModelReleaseBase = "https://github.com/Pozit1vchic/Parallel-Finder/releases/latest/download/";

bool isSafeModelFilename(const QString& filename)
{
    return !filename.isEmpty() && filename.endsWith(QStringLiteral(".onnx"), Qt::CaseInsensitive)
        && !filename.contains(QStringLiteral(".."))
        && !filename.contains(QLatin1Char('/')) && !filename.contains(QLatin1Char('\\'));
}

QString localPathFromInput(const QString& value)
{
    QString path = value.trimmed();
    if (path.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive)) {
        const QUrl url(path);
        if (url.isLocalFile()) path = url.toLocalFile();
    }
    path = QDir::fromNativeSeparators(path);
#ifdef Q_OS_WIN
    // QML FileDialog may return file:///D:/... while FFmpeg expects D:/...
    if (path.size() >= 3 && path.at(0) == QLatin1Char('/')
        && path.at(2) == QLatin1Char(':')) {
        path.remove(0, 1);
    }
#endif
    return QDir::cleanPath(path);
}

QStringList normalizedPaths(const QStringList& values)
{
    QStringList result;
    result.reserve(values.size());
    for (const QString& value : values) {
        const QString path = localPathFromInput(value);
        if (!path.isEmpty() && !result.contains(path)) result.push_back(path);
    }
    return result;
}

QString savePreview(const pfcore::DecodedFrame& frame, const QString& name)
{
    if (frame.rgba.empty() || frame.width <= 0 || frame.height <= 0) return {};
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/ParallelFinder/previews");
    QDir().mkpath(directory);
    const QString path = directory + QLatin1Char('/') + name;
    QImage image(frame.rgba.data(), frame.width, frame.height, QImage::Format_RGBA8888);
    QImage preview = image.copy();
    // Keep enough source detail for a large A/B viewport. The old 960x540
    // cap was visibly soft on 1440p/4K footage after the image was enlarged
    // by PreserveAspectFit.
    if (preview.width() > 1920 || preview.height() > 1080)
        preview = preview.scaled(1920, 1080, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (!preview.save(path, "PNG")) return {};
    // Image.source is a URL in QML.  A bare Windows path such as C:/tmp/a.png
    // may be parsed as a URL with the scheme "c" and fail silently.
    return QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
}

QString savePreviewAt(const std::string& source, double timestamp, const QString& name)
{
    try {
        pfcore::VideoDecoder decoder;
        decoder.open(source);
        decoder.seek(std::max(0.0, timestamp));
        pfcore::DecodedFrame frame;
        const double target = std::max(0.0, timestamp);
        while (decoder.readNext(frame)) {
            if (frame.timestampSeconds + 1e-3 >= target)
                return savePreview(frame, name);
        }
    } catch (...) {
    }
    return {};
}

std::vector<std::uint8_t> sceneThumbnail(const pfcore::DecodedFrame& frame,
                                         int width = 64, int height = 36)
{
    std::vector<std::uint8_t> result(static_cast<std::size_t>(width)
                                     * static_cast<std::size_t>(height) * 4U);
    if (frame.width <= 0 || frame.height <= 0 || frame.rgba.empty()) return result;
    for (int y = 0; y < height; ++y) {
        const int sourceY = std::min(frame.height - 1, (y * frame.height) / height);
        for (int x = 0; x < width; ++x) {
            const int sourceX = std::min(frame.width - 1, (x * frame.width) / width);
            const std::size_t source = (static_cast<std::size_t>(sourceY)
                                        * static_cast<std::size_t>(frame.width)
                                        + static_cast<std::size_t>(sourceX)) * 4U;
            const std::size_t target = (static_cast<std::size_t>(y)
                                        * static_cast<std::size_t>(width)
                                        + static_cast<std::size_t>(x)) * 4U;
            if (source + 3U >= frame.rgba.size()) continue;
            std::copy_n(frame.rgba.begin() + static_cast<std::ptrdiff_t>(source), 4,
                        result.begin() + static_cast<std::ptrdiff_t>(target));
        }
    }
    return result;
}

template <typename T>
void appendBytes(std::vector<std::uint8_t>& output, const T& value)
{
    const auto* begin = reinterpret_cast<const std::uint8_t*>(&value);
    output.insert(output.end(), begin, begin + sizeof(T));
}

template <typename T>
bool readBytes(const std::vector<std::uint8_t>& input, std::size_t& offset, T& value)
{
    if (offset + sizeof(T) > input.size()) return false;
    std::memcpy(&value, input.data() + offset, sizeof(T));
    offset += sizeof(T);
    return true;
}

std::vector<std::uint8_t> serializeMotionWindows(const std::vector<pfcore::MotionWindow>& windows)
{
    std::vector<std::uint8_t> output;
    const std::uint32_t version = 4;
    appendBytes(output, version);
    appendBytes(output, static_cast<std::uint32_t>(windows.size()));
    for (const auto& window : windows) {
        appendBytes(output, static_cast<std::uint32_t>(window.sourceId.size()));
        output.insert(output.end(), window.sourceId.begin(), window.sourceId.end());
        appendBytes(output, static_cast<std::uint64_t>(window.trackId));
        appendBytes(output, static_cast<std::uint64_t>(window.sceneIndex));
        appendBytes(output, static_cast<std::uint8_t>(window.hasSceneIndex ? 1 : 0));
        appendBytes(output, window.appearanceConfidence);
        appendBytes(output, static_cast<std::uint32_t>(window.appearanceEmbedding.size()));
        for (const float value : window.appearanceEmbedding) appendBytes(output, value);
        appendBytes(output, static_cast<std::uint32_t>(window.frames.size()));
        for (const auto& frame : window.frames) {
            appendBytes(output, frame.timestampSeconds);
            appendBytes(output, static_cast<std::uint32_t>(frame.keypoints.size()));
            for (const auto& point : frame.keypoints) {
                appendBytes(output, point.x);
                appendBytes(output, point.y);
                appendBytes(output, point.confidence);
            }
        }
    }
    return output;
}

bool deserializeMotionWindows(const std::vector<std::uint8_t>& input,
                              std::vector<pfcore::MotionWindow>& windows)
{
    std::size_t offset = 0;
    std::uint32_t version = 0, windowCount = 0;
    if (!readBytes(input, offset, version) || version != 4
        || !readBytes(input, offset, windowCount) || windowCount > 1'000'000U) return false;
    windows.clear();
    windows.reserve(windowCount);
    for (std::uint32_t windowIndex = 0; windowIndex < windowCount; ++windowIndex) {
        std::uint32_t sourceSize = 0;
        if (!readBytes(input, offset, sourceSize) || sourceSize > 16U * 1024U
            || offset + sourceSize > input.size()) return false;
        pfcore::MotionWindow window;
        window.sourceId.assign(reinterpret_cast<const char*>(input.data() + offset), sourceSize);
        offset += sourceSize;
        std::uint64_t trackId = 0, sceneIndex = 0;
        std::uint8_t hasSceneIndex = 0;
        if (!readBytes(input, offset, trackId) || !readBytes(input, offset, sceneIndex)
            || !readBytes(input, offset, hasSceneIndex)) return false;
        window.trackId = static_cast<std::size_t>(trackId);
        window.sceneIndex = static_cast<std::size_t>(sceneIndex);
        window.hasSceneIndex = hasSceneIndex != 0;
        std::uint32_t embeddingSize = 0;
        if (!readBytes(input, offset, window.appearanceConfidence)
            || !readBytes(input, offset, embeddingSize)
            || embeddingSize > 4096U) return false;
        window.appearanceEmbedding.resize(embeddingSize);
        for (float& value : window.appearanceEmbedding) {
            if (!readBytes(input, offset, value) || !std::isfinite(value)) return false;
        }
        std::uint32_t frameCount = 0;
        if (!readBytes(input, offset, frameCount) || frameCount > 100'000U) return false;
        window.frames.reserve(frameCount);
        for (std::uint32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
            pfcore::PoseFrame frame;
            std::uint32_t pointCount = 0;
            if (!readBytes(input, offset, frame.timestampSeconds)
                || !readBytes(input, offset, pointCount) || pointCount > 128U) return false;
            frame.keypoints.resize(pointCount);
            for (auto& point : frame.keypoints) {
                if (!readBytes(input, offset, point.x) || !readBytes(input, offset, point.y)
                    || !readBytes(input, offset, point.confidence)) return false;
            }
            window.frames.push_back(std::move(frame));
        }
        windows.push_back(std::move(window));
    }
    return offset == input.size();
}

std::filesystem::path findPoseModel()
{
    if (const char* configured = std::getenv("PF_MODEL_PATH"); configured && *configured) {
        const std::filesystem::path path(configured);
        if (std::filesystem::is_regular_file(path)) return path;
    }
    std::string settingsError;
    const pfservices::Settings settings = pfservices::SettingsStore().load(settingsError);
    if (!settings.modelPath.empty()) {
        const std::filesystem::path configured(settings.modelPath);
        if (std::filesystem::is_regular_file(configured)) return configured;
        const auto requestedName = configured.filename();
        if (!requestedName.empty()) {
            const auto executableModels = std::filesystem::path(QCoreApplication::applicationDirPath().toStdWString()) / "models";
            const auto localModelsRoot = std::filesystem::path(pfservices::SettingsStore::defaultDirectory()) / "models";
            for (const auto& root : {executableModels, localModelsRoot}) {
                const auto candidate = root / requestedName;
                if (std::filesystem::is_regular_file(candidate)) return candidate;
            }
        }
    }
    // Legacy developer location remains a fallback only. It must never
    // override a model explicitly selected in settings.json.
    const std::filesystem::path external = R"(D:\PF_CUDA\models\yolo26m-pose-640-b1.onnx)";
    if (std::filesystem::is_regular_file(external)) return external;
    const auto besideExecutable = std::filesystem::path(QCoreApplication::applicationDirPath().toStdWString())
        / "models" / "yolo26m-pose-640-b1.onnx";
    if (std::filesystem::is_regular_file(besideExecutable)) return besideExecutable;
    const std::filesystem::path localModels = std::filesystem::path(pfservices::SettingsStore::defaultDirectory())
        / "models" / "yolo26m-pose-640-b1.onnx";
    if (std::filesystem::is_regular_file(localModels)) return localModels;
    const auto executableModels = std::filesystem::path(QCoreApplication::applicationDirPath().toStdWString()) / "models";
    const auto localManifest = std::filesystem::path(pfservices::SettingsStore::defaultDirectory())
        / "models" / "manifest.json";
    const auto executableManifest = executableModels / "manifest.json";
    for (const auto& manifest : {executableManifest, localManifest}) {
        std::string manifestError;
        const auto asset = pfservices::ModelStore::readManifest(manifest,
            "yolo26m-pose-640-b1.onnx", manifestError);
        if (!asset.has_value()) continue;
        std::string downloadError;
        if (pfservices::ModelStore::download(*asset, localModels, {}, downloadError))
            return localModels;
    }
    std::string modelError;
    const pfservices::ModelAsset asset;
    if (const auto resolved = pfservices::ModelStore::resolve(
            asset, std::filesystem::path(QCoreApplication::applicationDirPath().toStdWString()), modelError)) {
        return *resolved;
    }
    (void)modelError;
    return {};
}

std::filesystem::path findBodyReIdModel()
{
    if (const char* configured = std::getenv("PF_REID_MODEL_PATH"); configured && *configured) {
        const std::filesystem::path path(configured);
        if (std::filesystem::is_regular_file(path)) return path;
    }
    const auto executableModels = std::filesystem::path(QCoreApplication::applicationDirPath().toStdWString()) / "models";
    const auto localModels = std::filesystem::path(pfservices::SettingsStore::defaultDirectory()) / "models";
    std::vector<std::filesystem::path> roots {executableModels, localModels};
    if (const char* root = std::getenv("PF_MODEL_ROOT"); root && *root)
        roots.emplace_back(root);
    roots.emplace_back(R"(D:\PF_CUDA\models)");
    const std::vector<std::string> preferred {
        "person-reid-osnet.onnx", "osnet_x1_0.onnx", "osnet.onnx",
        "body-reid.onnx", "person-reid.onnx"
    };
    for (const auto& root : roots) {
        for (const auto& name : preferred) {
            const auto candidate = root / name;
            if (std::filesystem::is_regular_file(candidate)) return candidate;
        }
    }
    // A release may publish the optional ReID asset in the same manifest as
    // pose models. Download only when the manifest explicitly describes the
    // file (URL/hash/size validation remains ModelStore's responsibility).
    const auto localManifest = localModels / "manifest.json";
    const auto executableManifest = executableModels / "manifest.json";
    for (const auto& name : preferred) {
        for (const auto& manifest : {executableManifest, localManifest}) {
            std::string manifestError;
            const auto asset = pfservices::ModelStore::readManifest(manifest, name, manifestError);
            if (!asset.has_value()) continue;
            std::string downloadError;
            const auto destination = localModels / name;
            if (pfservices::ModelStore::download(*asset, destination, {}, downloadError))
                return destination;
        }
    }
    // Accept custom release names as long as they clearly identify a ReID
    // export.  Never scan outside explicitly supported model roots.
    for (const auto& root : roots) {
        std::error_code error;
        if (!std::filesystem::is_directory(root, error)) continue;
        for (const auto& entry : std::filesystem::directory_iterator(root, error)) {
            if (error || !entry.is_regular_file(error)) continue;
            const auto extension = entry.path().extension().string();
            std::string name = entry.path().filename().string();
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char value) {
                return static_cast<char>(std::tolower(value));
            });
            if (extension == ".onnx"
                && (name.find("reid") != std::string::npos
                    || name.find("osnet") != std::string::npos)) {
                return entry.path();
            }
        }
    }
    return {};
}

struct AppearancePrototype {
    std::vector<float> embedding;
    double confidence = 0.0;
};

AppearancePrototype averageAppearance(const pfcore::PersonTrack& track,
                                      double startSeconds,
                                      double endSeconds)
{
    std::vector<float> sum;
    std::size_t samples = 0;
    std::size_t observations = 0;
    for (const auto& observation : track.observations) {
        if (observation.timestampSeconds < startSeconds
            || observation.timestampSeconds >= endSeconds) continue;
        ++observations;
        if (observation.appearanceEmbedding.empty()) continue;
        if (sum.empty()) sum.assign(observation.appearanceEmbedding.size(), 0.0F);
        if (sum.size() != observation.appearanceEmbedding.size()) continue;
        for (std::size_t index = 0; index < sum.size(); ++index)
            sum[index] += observation.appearanceEmbedding[index];
        ++samples;
    }
    if (samples == 0 || sum.empty()) return {};
    double norm = 0.0;
    for (float& value : sum) {
        value /= static_cast<float>(samples);
        norm += static_cast<double>(value) * value;
    }
    if (!(norm > 1e-12)) return {};
    norm = std::sqrt(norm);
    for (float& value : sum) value = static_cast<float>(value / norm);
    return {std::move(sum), static_cast<double>(samples)
        / static_cast<double>(std::max<std::size_t>(1, observations))};
}

} // namespace

AnalysisController* AnalysisController::instance()
{
    static AnalysisController controller;
    return &controller;
}

void AnalysisController::registerQmlTypes()
{
    qmlRegisterSingletonInstance("PfUiBridge", 1, 0, "Analysis", instance());
}

AnalysisController::AnalysisController(QObject* parent) : QObject(parent)
{
    std::string error;
    const auto settings = pfservices::SettingsStore().load(error);
    similarityThreshold_ = std::clamp(settings.similarityThreshold, 0.0, 1.0);
    candidateThreshold_ = std::clamp(settings.candidateThreshold, 0.0, 1.0);
    repeatGap_ = std::clamp(settings.minRepeatGapSec, 0.0, 30.0);
    sameFileGap_ = std::clamp(settings.sameFileGapSec, 0.0, 15.0);
    crossFileGap_ = std::clamp(settings.crossFileGapSec, 0.0, 15.0);
    duplicateWindow_ = std::clamp(settings.duplicateWindowSec, 0.25, 8.0);
    noiseFactor_ = std::clamp(settings.noiseFactor, 0.0, 2.0);
    maxUniqueResults_ = std::clamp(static_cast<int>(settings.maxUniqueResults), 10, 500);
    timeWeight_ = std::clamp(settings.timeWeight, 0.0, 1.0);
    providerChoice_ = QString::fromStdString(settings.provider);
    qualityProfile_ = QStringLiteral("maximum");
    if (settings.qualityProfile == "fast" || settings.qualityProfile == "medium" || settings.qualityProfile == "maximum")
        qualityProfile_ = QString::fromStdString(settings.qualityProfile);
    normalizeSize_ = settings.normalizeSize;
    mirrorPoses_ = settings.mirrorPoses;
    modelPath_ = QString::fromStdString(settings.modelPath);
    modelChoice_ = QFileInfo(modelPath_).fileName();
    if (modelChoice_.isEmpty()) modelChoice_ = QStringLiteral("yolo26m-pose-640-b1.onnx");
    cachePath_ = QString::fromStdString(settings.cachePath);
    cacheLimitGb_ = static_cast<double>(settings.cacheLimitBytes) / (1024.0 * 1024.0 * 1024.0);
    processingThreads_ = static_cast<int>(std::clamp<std::size_t>(settings.processingThreads, 0, 256));
    sceneThreshold_ = settings.sceneThreshold;
    const auto near = [](double left, double right) { return std::abs(left - right) < 1e-6; };
    if (near(similarityThreshold_, 0.72) && near(candidateThreshold_, 0.40)
        && near(repeatGap_, 8.0) && near(sameFileGap_, 3.0)
        && near(duplicateWindow_, 2.0) && near(noiseFactor_, 1.25)
        && maxUniqueResults_ == 50 && near(timeWeight_, 0.10)) {
        accuracyPreset_ = QStringLiteral("fast");
    } else if (near(similarityThreshold_, 0.90) && near(candidateThreshold_, 0.70)
        && near(repeatGap_, 4.0) && near(sameFileGap_, 1.5)
        && near(duplicateWindow_, 1.0) && near(noiseFactor_, 0.70)
        && maxUniqueResults_ == 200 && near(timeWeight_, 0.40)) {
        accuracyPreset_ = QStringLiteral("precise");
    } else if (!near(similarityThreshold_, 0.85) || !near(candidateThreshold_, 0.55)
        || !near(repeatGap_, 6.0) || !near(sameFileGap_, 2.0)
        || !near(duplicateWindow_, 1.5) || !near(noiseFactor_, 1.0)
        || maxUniqueResults_ != 100 || !near(timeWeight_, 0.25)) {
        accuracyPreset_ = QStringLiteral("custom");
    }
}

void AnalysisController::saveMatcherSettings() const
{
    std::string error;
    pfservices::SettingsStore store;
    auto settings = store.load(error);
    settings.similarityThreshold = similarityThreshold_;
    settings.candidateThreshold = candidateThreshold_;
    settings.minRepeatGapSec = repeatGap_;
    settings.sameFileGapSec = sameFileGap_;
    settings.crossFileGapSec = crossFileGap_;
    settings.duplicateWindowSec = duplicateWindow_;
    settings.noiseFactor = noiseFactor_;
    settings.maxUniqueResults = static_cast<std::size_t>(maxUniqueResults_);
    settings.timeWeight = timeWeight_;
    store.save(settings, error);
}

void AnalysisController::saveSettings() const
{
    std::string error;
    pfservices::SettingsStore store;
    auto settings = store.load(error);
    settings.provider = providerChoice_.toStdString();
    settings.qualityProfile = qualityProfile_.toStdString();
    settings.normalizeSize = normalizeSize_;
    settings.mirrorPoses = mirrorPoses_;
    settings.modelPath = modelPath_.toStdString();
    settings.cachePath = cachePath_.toStdString();
    settings.cacheLimitBytes = static_cast<std::size_t>(std::max(0.25, cacheLimitGb_) * 1024.0 * 1024.0 * 1024.0);
    settings.processingThreads = static_cast<std::size_t>(std::clamp(processingThreads_, 0, 256));
    settings.sceneThreshold = sceneThreshold_;
    settings.similarityThreshold = similarityThreshold_;
    settings.candidateThreshold = candidateThreshold_;
    settings.minRepeatGapSec = repeatGap_;
    settings.sameFileGapSec = sameFileGap_;
    settings.crossFileGapSec = crossFileGap_;
    settings.duplicateWindowSec = duplicateWindow_;
    settings.noiseFactor = noiseFactor_;
    settings.maxUniqueResults = static_cast<std::size_t>(maxUniqueResults_);
    settings.timeWeight = timeWeight_;
    store.save(settings, error);
}

void AnalysisController::setProgress(double value, const QString& stage, qlonglong processed, qlonglong total)
{
    progress_ = std::clamp(value, 0.0, 1.0);
    progressStage_ = stage;
    processedFrames_ = processed;
    totalFrames_ = total;
    emit progressChanged();
}

void AnalysisController::setSimilarityThreshold(double value)
{
    const double clamped = std::clamp(value, 0.0, 1.0);
    if (std::abs(similarityThreshold_ - clamped) < 1e-9) return;
    similarityThreshold_ = clamped;
    saveMatcherSettings();
    emit matcherParamsChanged();
}

void AnalysisController::setCandidateThreshold(double value)
{
    const double clamped = std::clamp(value, 0.0, 1.0);
    if (std::abs(candidateThreshold_ - clamped) < 1e-9) return;
    candidateThreshold_ = clamped;
    saveMatcherSettings();
    emit matcherParamsChanged();
}

void AnalysisController::setAccuracyPreset(const QString& value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized != QStringLiteral("fast") && normalized != QStringLiteral("balanced")
        && normalized != QStringLiteral("precise") && normalized != QStringLiteral("custom")) return;
    if (accuracyPreset_ == normalized) return;
    accuracyPreset_ = normalized;
    emit matcherParamsChanged();
}

void AnalysisController::setRepeatGap(double value)
{
    const double clamped = std::clamp(value, 0.0, 30.0);
    if (std::abs(repeatGap_ - clamped) < 1e-9) return;
    repeatGap_ = clamped;
    saveMatcherSettings();
    emit matcherParamsChanged();
}

void AnalysisController::setSameFileGap(double value)
{
    const double clamped = std::clamp(value, 0.0, 15.0);
    if (std::abs(sameFileGap_ - clamped) < 1e-9) return;
    sameFileGap_ = clamped;
    saveMatcherSettings();
    emit matcherParamsChanged();
}

void AnalysisController::setCrossFileGap(double value)
{
    const double clamped = std::clamp(value, 0.0, 15.0);
    if (std::abs(crossFileGap_ - clamped) < 1e-9) return;
    crossFileGap_ = clamped;
    saveMatcherSettings();
    emit matcherParamsChanged();
}

void AnalysisController::setDuplicateWindow(double value)
{
    const double clamped = std::clamp(value, 0.25, 8.0);
    if (std::abs(duplicateWindow_ - clamped) < 1e-9) return;
    duplicateWindow_ = clamped;
    saveMatcherSettings();
    emit matcherParamsChanged();
}

void AnalysisController::setNoiseFactor(double value)
{
    const double clamped = std::clamp(value, 0.0, 2.0);
    if (std::abs(noiseFactor_ - clamped) < 1e-9) return;
    noiseFactor_ = clamped;
    saveMatcherSettings();
    emit matcherParamsChanged();
}

void AnalysisController::setMaxUniqueResults(int value)
{
    const int clamped = std::clamp(value, 10, 500);
    if (maxUniqueResults_ == clamped) return;
    maxUniqueResults_ = clamped;
    saveMatcherSettings();
    emit matcherParamsChanged();
}

void AnalysisController::setTimeWeight(double value)
{
    const double clamped = std::clamp(value, 0.0, 1.0);
    if (std::abs(timeWeight_ - clamped) < 1e-9) return;
    timeWeight_ = clamped;
    saveMatcherSettings();
    emit matcherParamsChanged();
}

void AnalysisController::setProviderChoice(const QString& value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized != QStringLiteral("auto") && normalized != QStringLiteral("tensorrt")
        && normalized != QStringLiteral("cuda") && normalized != QStringLiteral("dml")
        && normalized != QStringLiteral("directml") && normalized != QStringLiteral("cpu")) {
        return;
    }
    const QString canonical = normalized == QStringLiteral("directml") ? QStringLiteral("dml") : normalized;
    if (providerChoice_ == canonical) return;
    providerChoice_ = canonical;
    qDebug().noquote() << "Parallel Finder compute backend:" << canonical;
    saveSettings();
    emit settingsChanged();
}

void AnalysisController::setQualityProfile(const QString& value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized != QStringLiteral("fast") && normalized != QStringLiteral("medium")
        && normalized != QStringLiteral("maximum")) return;
    if (qualityProfile_ == normalized) return;
    qualityProfile_ = normalized;
    saveSettings();
    emit settingsChanged();
}

void AnalysisController::setNormalizeSize(bool value)
{
    if (normalizeSize_ == value) return;
    normalizeSize_ = value;
    saveSettings();
    emit settingsChanged();
}

void AnalysisController::setMirrorPoses(bool value)
{
    if (mirrorPoses_ == value) return;
    mirrorPoses_ = value;
    saveSettings();
    emit settingsChanged();
}

void AnalysisController::setModelPath(const QString& value)
{
    const QString normalized = value.trimmed();
    if (modelPath_ == normalized) return;
    modelPath_ = normalized;
    saveSettings();
    emit settingsChanged();
}

void AnalysisController::selectModel(const QString& filename)
{
    const QString requested = QFileInfo(filename.trimmed()).fileName();
    if (!isSafeModelFilename(requested)) return;
    modelChoice_ = requested;
    emit settingsChanged();

    const auto modelsRoot = std::filesystem::path(pfservices::SettingsStore::defaultDirectory()) / "models";
    const auto destination = modelsRoot / requested.toStdWString();
    std::error_code filesystemError;
    if (std::filesystem::is_regular_file(destination, filesystemError)) {
        modelPath_ = QString::fromStdWString(destination.wstring());
        modelStatus_ = QStringLiteral("Модель готова к анализу");
        saveSettings();
        emit settingsChanged();
        emit modelStatusChanged();
        return;
    }
    const auto besideExecutable = std::filesystem::path(QCoreApplication::applicationDirPath().toStdWString())
        / "models" / requested.toStdWString();
    if (std::filesystem::is_regular_file(besideExecutable, filesystemError)) {
        modelPath_ = QString::fromStdWString(besideExecutable.wstring());
        modelStatus_ = QStringLiteral("Модель готова к анализу");
        saveSettings();
        emit settingsChanged();
        emit modelStatusChanged();
        return;
    }
    if (const char* root = std::getenv("PF_MODEL_ROOT"); root && *root) {
        const auto externalRoot = std::filesystem::path(root) / requested.toStdWString();
        if (std::filesystem::is_regular_file(externalRoot, filesystemError)) {
            modelPath_ = QString::fromStdWString(externalRoot.wstring());
            modelStatus_ = QStringLiteral("Модель готова к анализу");
            saveSettings();
            emit settingsChanged();
            emit modelStatusChanged();
            return;
        }
    }
    if (modelDownloading_) return;

    modelDownloading_ = true;
    modelStatus_ = QStringLiteral("Скачиваем модель с GitHub Releases…");
    emit modelStatusChanged();
    QThread* thread = QThread::create([this, requested, destination] {
        pfservices::ModelAsset asset;
        asset.filename = requested.toStdString();
        asset.downloadUrl = std::string(kModelReleaseBase) + asset.filename;
        std::string error;
        const bool ok = pfservices::ModelStore::download(asset, destination, {}, error);
        const QString message = ok
            ? QStringLiteral("Модель скачана и готова")
            : QStringLiteral("Не удалось скачать модель: ") + QString::fromStdString(error);
        QMetaObject::invokeMethod(this, [this, ok, destination, message] {
            modelDownloading_ = false;
            modelStatus_ = message;
            if (ok) {
                modelPath_ = QString::fromStdWString(destination.wstring());
                saveSettings();
                emit settingsChanged();
            }
            emit modelStatusChanged();
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void AnalysisController::setCachePath(const QString& value)
{
    const QString normalized = value.trimmed();
    if (cachePath_ == normalized) return;
    cachePath_ = normalized;
    saveSettings();
    emit settingsChanged();
}

void AnalysisController::setCacheLimitGb(double value)
{
    const double clamped = std::clamp(value, 0.25, 128.0);
    if (std::abs(cacheLimitGb_ - clamped) < 1e-9) return;
    cacheLimitGb_ = clamped;
    saveSettings();
    emit settingsChanged();
}

void AnalysisController::setProcessingThreads(int value)
{
    const int clamped = std::clamp(value, 0, 256);
    if (processingThreads_ == clamped) return;
    processingThreads_ = clamped;
    saveSettings();
    emit settingsChanged();
}

void AnalysisController::setSceneThreshold(double value)
{
    const double clamped = std::clamp(value, 1.0, 255.0);
    if (std::abs(sceneThreshold_ - clamped) < 1e-9) return;
    sceneThreshold_ = clamped;
    saveSettings();
    emit settingsChanged();
}

bool AnalysisController::exportResults(const QString& format,
                                       int numberingMode,
                                       int cutMode,
                                       const QString& outputFolder,
                                       const QString& prefix,
                                       const QVariantList& selectedIndexes)
{
    std::vector<pfcore::MotionMatch> selected;
    for (const QVariant& value : selectedIndexes) {
        bool ok = false;
        const int index = value.toInt(&ok);
        if (ok && index >= 0 && index < static_cast<int>(matches_.size()))
            selected.push_back(matches_[static_cast<std::size_t>(index)]);
    }
    if (selected.empty()) {
        emit exportFinished(false, QStringLiteral("Не выбраны результаты для экспорта"));
        return false;
    }
    pfexporters::ExportOptions options;
    const QString normalized = format.trimmed().toUpper();
    if (normalized == QStringLiteral("CSV")) options.format = pfexporters::ExportFormat::Csv;
    else if (normalized == QStringLiteral("TXT")) options.format = pfexporters::ExportFormat::Txt;
    else if (normalized == QStringLiteral("EDL")) options.format = pfexporters::ExportFormat::Edl;
    else if (normalized == QStringLiteral("FCPXML")) options.format = pfexporters::ExportFormat::FcpXml;
    else if (normalized == QStringLiteral("AEP")) options.format = pfexporters::ExportFormat::Aep;
    else options.format = pfexporters::ExportFormat::Json;
    options.numbering = numberingMode == 1 ? pfexporters::NumberingMode::RenumberSorted
                                           : pfexporters::NumberingMode::AsInVideo;
    options.cutMode = cutMode == 1 ? pfexporters::CutMode::Fast : pfexporters::CutMode::Exact;
    options.outputFolder = outputFolder.toStdString();
    options.filePrefix = prefix.trimmed().isEmpty() ? "frame_" : prefix.trimmed().toStdString();
    options.framesPerSecond = sourceFps_;
    std::string error;
    const bool ok = pfexporters::writeResults(selected, options, error);
    emit exportFinished(ok, ok ? QStringLiteral("Экспорт завершён") : QString::fromStdString(error));
    return ok;
}

bool AnalysisController::exportTheme(const QString& path, const QVariantMap& theme) const
{
    QString localPath = path;
    if (localPath.startsWith(QStringLiteral("file:"))) localPath = QUrl(localPath).toLocalFile();
    if (localPath.isEmpty()) return false;
    QJsonObject object = QJsonObject::fromVariantMap(theme);
    object.insert(QStringLiteral("schema"), QStringLiteral("parallel-finder-theme"));
    object.insert(QStringLiteral("version"), 1);
    QFile file(localPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const bool written = file.write(QJsonDocument(object).toJson(QJsonDocument::Indented)) >= 0;
    file.close();
    return written;
}

QVariantMap AnalysisController::importTheme(const QString& path) const
{
    QString localPath = path;
    if (localPath.startsWith(QStringLiteral("file:"))) localPath = QUrl(localPath).toLocalFile();
    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly)) return {};
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) return {};
    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("schema")).toString() != QStringLiteral("parallel-finder-theme")) return {};
    return object.toVariantMap();
}

void AnalysisController::setStatus(const QString& status)
{
    if (status_ == status) return;
    status_ = status;
    emit statusChanged();
}

void AnalysisController::inspectFiles(const QStringList& paths)
{
    if (busy_) return;
    const QStringList normalized = normalizedPaths(paths);
    deferredAnalyzePaths_.clear();
    results_.clear();
    matches_.clear();
    matchCount_ = 0;
    analysisCompleted_ = false;
    emit analysisStateChanged();
    emit resultsChanged();
    emit summaryChanged();
    busy_ = true;
    emit busyChanged();
    setProgress(0.0, QStringLiteral("Открываем файлы"), 0, 0);
    setStatus(QStringLiteral("Открываем видео…"));
    QThread* thread = QThread::create([this, paths = normalized] {
        int files = 0;
        qlonglong frames = 0;
        double duration = 0.0;
        QString error;
        for (const QString& path : paths) {
            try {
                pfcore::VideoDecoder decoder;
                decoder.open(path.toStdString());
                const auto& info = decoder.info();
                ++files;
                duration += info.durationSeconds;
                if (info.frameRate > 0.0) frames += static_cast<qlonglong>(info.durationSeconds * info.frameRate);
            } catch (const std::exception& exception) {
                error = QString::fromUtf8(exception.what());
                break;
            }
        }
        QMetaObject::invokeMethod(this, [this, files, frames, duration, error] {
            fileCount_ = files;
            frameCount_ = frames;
            durationSeconds_ = duration;
            emit summaryChanged();
            busy_ = false;
            emit busyChanged();
            // File inspection only validates metadata; it is not an analysis
            // run and must not present a misleading 100% progress state.
            setProgress(0.0, error.isEmpty() ? QStringLiteral("Файлы готовы") : QStringLiteral("Ошибка чтения"), 0, 0);
            setStatus(error.isEmpty() ? QStringLiteral("Файлы готовы к анализу")
                                      : QStringLiteral("Не удалось открыть файл: ") + error);
            if (error.isEmpty() && !deferredAnalyzePaths_.isEmpty()) {
                const QStringList queuedPaths = deferredAnalyzePaths_;
                deferredAnalyzePaths_.clear();
                QMetaObject::invokeMethod(this, [this, queuedPaths] {
                    analyzeFiles(queuedPaths);
                }, Qt::QueuedConnection);
            }
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

QStringList AnalysisController::filesInFolder(const QString& folder) const
{
    const QString path = localPathFromInput(folder);
    QDir directory(path);
    const QStringList filters { QStringLiteral("*.mp4"), QStringLiteral("*.mov"), QStringLiteral("*.mkv"), QStringLiteral("*.avi"), QStringLiteral("*.webm"), QStringLiteral("*.m4v") };
    QStringList files;
    for (const QFileInfo& info : directory.entryInfoList(filters, QDir::Files | QDir::Readable, QDir::Name))
        files.push_back(info.absoluteFilePath());
    return files;
}

void AnalysisController::analyzeFiles(const QStringList& paths)
{
    const QStringList normalized = normalizedPaths(paths);
    if (normalized.isEmpty()) {
        analysisCompleted_ = false;
        emit analysisStateChanged();
        setProgress(0.0, QStringLiteral("Нет файлов"), 0, 0);
        setStatus(QStringLiteral("Добавьте хотя бы одно видео для анализа"));
        return;
    }
    if (busy_) {
        if (!normalized.isEmpty()) deferredAnalyzePaths_ = normalized;
        return;
    }
    // A manually selected execution provider is a hard requirement. Do not
    // silently fall back to CPU after the user explicitly chose CUDA, TensorRT
    // or DirectML; report the real probe reason before touching the files.
    if (const auto requested = pfgpu::parseProvider(providerChoice_.toStdString());
        requested.has_value() && *requested != pfgpu::Provider::Auto
        && !pfgpu::isProviderAvailable(*requested)) {
        const auto* status = pfgpu::findBackendStatus(*requested);
        const QString reason = status && !status->reason.empty()
            ? QStringLiteral(": ") + QString::fromStdString(status->reason)
            : QString();
        setProgress(0.0, QStringLiteral("Провайдер недоступен"), 0, 0);
        setStatus(QStringLiteral("Выбранный провайдер ")
                  + QString::fromLatin1(pfgpu::providerName(*requested))
                  + QStringLiteral(" недоступен") + reason);
        return;
    }
    results_.clear();
    matches_.clear();
    matchCount_ = 0;
    analysisCompleted_ = false;
    emit analysisStateChanged();
    emit resultsChanged();
    emit summaryChanged();
    busy_ = true;
    emit busyChanged();
    setProgress(0.0, QStringLiteral("Подготавливаем анализ"), 0, 0);
    setStatus(QStringLiteral("Декодируем кадры и ищем смены сцен…"));
    const double similarityThreshold = similarityThreshold_;
    const double candidateThreshold = candidateThreshold_;
    const double repeatGap = repeatGap_;
    const double sameFileGap = sameFileGap_;
    const double crossFileGap = crossFileGap_;
    const double duplicateWindow = duplicateWindow_;
    const double noiseFactor = noiseFactor_;
    const int maxUniqueResults = maxUniqueResults_;
    const double timeWeight = timeWeight_;
    const QString providerChoice = providerChoice_;
    const QString qualityProfile = qualityProfile_;
    const bool normalizeSize = normalizeSize_;
    const bool mirrorPoses = mirrorPoses_;
    const QString previewToken = QString::number(QDateTime::currentMSecsSinceEpoch());
    QThread* thread = QThread::create([this, paths = normalized, similarityThreshold, candidateThreshold, repeatGap,
                                        sameFileGap, crossFileGap, duplicateWindow, noiseFactor,
                                        maxUniqueResults, timeWeight, providerChoice,
                                        qualityProfile, normalizeSize, mirrorPoses, previewToken] {
        try {
        int files = 0;
        int scenes = 0;
        int poseDetections = 0;
        int matches = 0;
        QVariantList resultRecords;
        std::vector<pfcore::MotionMatch> foundMatches;
        QStringList previewA;
        QStringList previewB;
        qlonglong frames = 0;
        double duration = 0.0;
        double sourceFps = 0.0;
        QString error;
        qlonglong totalFramesEstimate = 0;
        qlonglong processedFrames = 0;
        std::string settingsError;
        const pfservices::Settings settings = pfservices::SettingsStore().load(settingsError);
        (void)settingsError;
        const auto model = findPoseModel();
        if (model.empty()) {
            QMetaObject::invokeMethod(this, [this] {
                busy_ = false;
                emit busyChanged();
                setProgress(0.0, QStringLiteral("Модель не найдена"), 0, 0);
                setStatus(QStringLiteral("Модель поз не найдена. Укажите её в settings.json или в папке models."));
            }, Qt::QueuedConnection);
            return;
        }
        for (const QString& path : paths) {
            try {
                pfcore::VideoDecoder probe;
                probe.open(path.toStdString());
                const auto& info = probe.info();
                totalFramesEstimate += static_cast<qlonglong>(std::max(1.0, info.durationSeconds * info.frameRate));
            } catch (...) {
            }
        }
        QMetaObject::invokeMethod(this, [this, totalFramesEstimate] {
            setProgress(0.0, QStringLiteral("Читаем кадры"), 0, totalFramesEstimate);
        }, Qt::QueuedConnection);
        std::unique_ptr<pfservices::PfCache> analysisCache;
        try {
            const std::filesystem::path cacheRoot = settings.cachePath.empty()
                ? std::filesystem::path(pfservices::SettingsStore::defaultDirectory()) / "cache"
                : std::filesystem::path(settings.cachePath);
            analysisCache = std::make_unique<pfservices::PfCache>(cacheRoot, settings.cacheLimitBytes);
        } catch (const std::exception&) {
            // Cache is an optimization; analysis remains usable when its path
            // is unavailable or malformed.
        }
        std::unique_ptr<pfgpu::PoseEstimator> pose;
        {
            pfgpu::PoseEstimatorParams poseParams;
            if (const auto provider = pfgpu::parseProvider(providerChoice.toStdString()); provider.has_value()) {
                poseParams.provider = *provider;
            }
            poseParams.intraOpThreads = settings.processingThreads;
            pose = std::make_unique<pfgpu::PoseEstimator>(model.string(), poseParams);
        }
        const auto reidModel = findBodyReIdModel();
        const bool reidModelPresent = !reidModel.empty();
        bool reidReady = false;
        QString reidFailure;
        std::unique_ptr<pfgpu::ReIdEstimator> reid;
        if (reidModelPresent) {
            try {
                pfgpu::ReIdEstimatorParams reidParams;
                if (const auto provider = pfgpu::parseProvider(providerChoice.toStdString()); provider.has_value())
                    reidParams.provider = *provider;
                reidParams.intraOpThreads = settings.processingThreads;
                reid = std::make_unique<pfgpu::ReIdEstimator>(reidModel.string(), reidParams);
                reidReady = true;
            } catch (const std::exception& exception) {
                reidFailure = QString::fromUtf8(exception.what());
            }
        }
        std::vector<pfcore::MotionWindow> windows;
        for (const QString& path : paths) {
            try {
                pfcore::VideoDecoder decoder;
                decoder.open(path.toStdString());
                const auto info = decoder.info();
                std::vector<pfcore::MotionWindow> cachedWindows;
                bool cacheHit = false;
                std::string cacheKey = "motion-v4|" + model.string() + "|"
                    + providerChoice.toStdString() + "|reid="
                    + (reidModelPresent ? reidModel.string() : std::string("none")) + "|"
                    + path.toStdString();
                std::error_code cacheFileError;
                const auto cacheFileSize = std::filesystem::file_size(path.toStdString(), cacheFileError);
                const auto cacheWriteTime = std::filesystem::last_write_time(path.toStdString(), cacheFileError);
                cacheKey += "|" + std::to_string(cacheFileSize) + "|"
                    + std::to_string(cacheWriteTime.time_since_epoch().count());
                if (analysisCache) {
                    if (const auto cached = analysisCache->get(cacheKey))
                        cacheHit = deserializeMotionWindows(*cached, cachedWindows);
                }
                ++files;
                duration += info.durationSeconds;
                if (sourceFps <= 0.0) sourceFps = info.frameRate;
                frames += static_cast<qlonglong>(std::max(0.0, info.durationSeconds * info.frameRate));
                std::vector<std::vector<std::uint8_t>> sceneBuffers;
                std::vector<double> sceneTimestamps;
                sceneBuffers.reserve(static_cast<std::size_t>(std::max(1.0, info.durationSeconds)) + 1U);
                sceneTimestamps.reserve(sceneBuffers.capacity());
                pfcore::PersonTracker tracker;
                pfcore::DecodedFrame firstFrame;
                pfcore::DecodedFrame lastFrame;
                pfcore::DecodedFrame frame;
                std::size_t poseSampleIndex = 0;
                double nextSceneSample = 0.0;
                double previousPoseTimestamp = -1.0;
                // The profile names are user-facing contracts: fast samples
                // every fifth frame, medium every third, maximum every frame.
                const std::size_t poseStride = qualityProfile == QStringLiteral("fast") ? 5U
                    : qualityProfile == QStringLiteral("medium") ? 3U : 1U;
                // Appearance is more stable than frame-to-frame pose and does
                // not need to run at the full detector cadence. This keeps the
                // optional model affordable while providing several samples per
                // one-second temporal window.
                const std::size_t reidStride = qualityProfile == QStringLiteral("fast") ? 3U : 2U;
                while (decoder.readNext(frame)) {
                    ++processedFrames;
                    if (processedFrames % 10 == 0 || processedFrames == totalFramesEstimate) {
                        const double localProgress = totalFramesEstimate > 0
                            ? static_cast<double>(processedFrames) / static_cast<double>(totalFramesEstimate)
                            : 0.0;
                        QMetaObject::invokeMethod(this, [this, localProgress, processedFrames, totalFramesEstimate] {
                            setProgress(localProgress * 0.85, QStringLiteral("Анализируем движение"), processedFrames, totalFramesEstimate);
                        }, Qt::QueuedConnection);
                    }
                    if (firstFrame.rgba.empty()) firstFrame = frame;
                    if (frame.timestampSeconds + 1e-9 >= nextSceneSample) {
                        sceneBuffers.push_back(sceneThumbnail(frame));
                        sceneTimestamps.push_back(frame.timestampSeconds);
                        do { nextSceneSample += 1.0; }
                        while (nextSceneSample <= frame.timestampSeconds + 1e-9);
                    }
                    if (pose && !cacheHit && (poseSampleIndex++ % poseStride) == 0U) {
                        pfgpu::PoseImage image{frame.width, frame.height, frame.rgba.data()};
                        const auto detections = pose->infer(image);
                        poseDetections += static_cast<int>(detections.size());
                        std::vector<pfcore::PersonDetection> frameDetections;
                        frameDetections.reserve(detections.size());
                        const bool sampleAppearance = reid && ((poseSampleIndex - 1U) % reidStride == 0U);
                        std::vector<pfgpu::ReIdImage> reidImages;
                        if (sampleAppearance) reidImages.reserve(detections.size());
                        for (const auto& detection : detections) {
                            pfcore::PersonDetection person;
                            person.timestampSeconds = frame.timestampSeconds;
                            person.box = {detection.left, detection.top, detection.right, detection.bottom};
                            person.confidence = detection.confidence;
                            for (std::size_t i = 0; i + 2 < detection.keypoints.size(); i += 3) {
                                person.keypoints.push_back({detection.keypoints[i], detection.keypoints[i + 1], detection.keypoints[i + 2]});
                                person.keypointConfidence += detection.keypoints[i + 2];
                            }
                            if (!person.keypoints.empty()) {
                                person.keypointConfidence /= static_cast<double>(person.keypoints.size());
                            }
                            if (mirrorPoses) {
                                for (auto& point : person.keypoints) point.x = 1.0 - point.x;
                            }
                            frameDetections.push_back(std::move(person));
                            if (sampleAppearance) {
                                reidImages.push_back({frame.width, frame.height, frame.rgba.data(),
                                                      detection.left, detection.top,
                                                      detection.right, detection.bottom});
                            }
                        }
                        if (sampleAppearance && !reidImages.empty()) {
                            try {
                                const auto embeddings = reid->inferBatch(reidImages);
                                for (std::size_t detection = 0;
                                     detection < std::min(embeddings.size(), frameDetections.size());
                                     ++detection) {
                                    frameDetections[detection].appearanceEmbedding = embeddings[detection];
                                    frameDetections[detection].appearanceConfidence = embeddings[detection].empty() ? 0.0 : 1.0;
                                }
                            } catch (const std::exception& exception) {
                                reidFailure = QString::fromUtf8(exception.what());
                                reid.reset();
                                reidReady = false;
                            }
                        }
                        const double frameDuration = previousPoseTimestamp >= 0.0
                            ? std::max(0.0, frame.timestampSeconds - previousPoseTimestamp)
                            : (info.frameRate > 0.0 ? 1.0 / info.frameRate : 0.0);
                        tracker.update(frame.timestampSeconds, frameDuration, frameDetections);
                        previousPoseTimestamp = frame.timestampSeconds;
                    }
                    lastFrame = std::move(frame);
                }
                std::vector<pfcore::SceneSample> samples;
                samples.reserve(sceneBuffers.size());
                for (std::size_t sample = 0; sample < sceneBuffers.size(); ++sample)
                    samples.push_back({sceneTimestamps[sample], 64, 36, sceneBuffers[sample]});
                pfcore::SceneDetector sceneDetector(settings.sceneThreshold,
                                                    std::max<std::size_t>(1,
                                                        static_cast<std::size_t>(std::ceil(
                                                            static_cast<double>(settings.sceneMinFrames)
                                                            / std::max(1.0, info.frameRate)))),
                                                    settings.sceneAdaptiveMultiplier);
                const auto sceneBoundaries = sceneDetector.detect(samples);
                // A boundary separates two scenes; the user-facing counter is
                // the number of actual segments, not the number of cuts.
                scenes += samples.empty() ? 0 : static_cast<int>(sceneBoundaries.size() + 1U);
                const QString previewStart = firstFrame.rgba.empty() ? QString()
                    : savePreview(firstFrame, QStringLiteral("%1_%2_start.png").arg(previewToken).arg(files));
                const QString previewEnd = lastFrame.rgba.empty() ? QString()
                    : savePreview(lastFrame, QStringLiteral("%1_%2_end.png").arg(previewToken).arg(files));
                if (cacheHit) {
                    for (auto& cached : cachedWindows) {
                        windows.push_back(std::move(cached));
                        previewA.push_back(previewStart);
                        previewB.push_back(previewEnd);
                    }
                } else if (pose) {
                    // Keep scene boundaries in the motion index: a candidate
                    // never crosses a shot change, and a clip can end only at
                    // the end of its scene rather than when a person briefly
                    // leaves the frame.
                    std::vector<double> sceneStarts {samples.empty() ? 0.0 : samples.front().timestampSeconds};
                    for (const auto& boundary : sceneBoundaries) sceneStarts.push_back(boundary.timestampSeconds);
                    const double lastTimestamp = samples.empty() ? info.durationSeconds : samples.back().timestampSeconds;
                    for (std::size_t sceneIndex = 0; sceneIndex < sceneStarts.size(); ++sceneIndex) {
                        const double sceneStart = sceneStarts[sceneIndex];
                        const double sceneEnd = sceneIndex + 1 < sceneStarts.size()
                            ? sceneStarts[sceneIndex + 1] : std::max(info.durationSeconds, lastTimestamp + 0.001);
                        // Keep every sufficiently long person track.  The old
                        // pipeline selected tracker.dominant(), which silently
                        // discarded all other people and made the result set
                        // look like one person was present in the whole clip.
                        for (const auto& track : tracker.tracks()) {
                            pfcore::MotionWindow window;
                            window.sourceId = path.toStdString();
                            window.trackId = track.id;
                            window.sceneIndex = sceneIndex;
                            window.hasSceneIndex = true;
                            const auto appearance = averageAppearance(track, sceneStart, sceneEnd);
                            window.appearanceEmbedding = appearance.embedding;
                            window.appearanceConfidence = appearance.confidence;
                            for (const auto& observation : track.observations) {
                                if (observation.timestampSeconds >= sceneStart
                                    && observation.timestampSeconds < sceneEnd) {
                                    window.frames.push_back({observation.timestampSeconds, observation.keypoints});
                                }
                            }
                            // Compare overlapping motion windows rather than one
                            // aggregate window per scene. The durations and stride
                            // are the documented 3b contract, measured on actual
                            // timestamps rather than guessed frame counts.
                            constexpr double windowSeconds = 1.5;
                            constexpr double strideSeconds = 0.25;
                            constexpr double minimumWindowSeconds = 1.1;
                            std::size_t start = 0;
                            while (start < window.frames.size()) {
                                const double startTime = window.frames[start].timestampSeconds;
                                if (startTime + minimumWindowSeconds > sceneEnd + 1e-9) break;
                                std::size_t end = start;
                                while (end + 1 < window.frames.size()
                                       && window.frames[end + 1].timestampSeconds
                                           <= startTime + windowSeconds + 1e-9) {
                                    ++end;
                                }
                                if (end > start
                                    && window.frames[end].timestampSeconds - startTime
                                        >= minimumWindowSeconds) {
                                    pfcore::MotionWindow chunk;
                                    chunk.sourceId = window.sourceId;
                                    chunk.trackId = window.trackId;
                                    chunk.sceneIndex = window.sceneIndex;
                                    chunk.hasSceneIndex = window.hasSceneIndex;
                                    chunk.appearanceEmbedding = window.appearanceEmbedding;
                                    chunk.appearanceConfidence = window.appearanceConfidence;
                                    chunk.frames.assign(window.frames.begin()
                                                            + static_cast<std::ptrdiff_t>(start),
                                                        window.frames.begin()
                                                            + static_cast<std::ptrdiff_t>(end + 1));
                                    windows.push_back(std::move(chunk));
                                    previewA.push_back(previewStart);
                                    previewB.push_back(previewEnd);
                                }
                                const double nextTime = startTime + strideSeconds;
                                std::size_t next = start + 1;
                                while (next < window.frames.size()
                                       && window.frames[next].timestampSeconds < nextTime) {
                                    ++next;
                                }
                                start = next;
                            }
                        }
                    }
                    if (analysisCache) {
                        std::vector<pfcore::MotionWindow> fileWindows;
                        // Only entries from this source are serialized, so a
                        // later run can skip pose inference for the same file.
                        for (const auto& candidate : windows) {
                            if (candidate.sourceId == path.toStdString()) fileWindows.push_back(candidate);
                        }
                        std::string cacheError;
                        analysisCache->put(cacheKey, serializeMotionWindows(fileWindows), cacheError);
                    }
                }
            } catch (const std::exception& exception) {
                error = QString::fromUtf8(exception.what());
                break;
            }
        }
        if (error.isEmpty() && windows.size() >= 2) {
            pfcore::MotionMatcherParams params;
            params.similarityThreshold = similarityThreshold;
            params.candidateThreshold = candidateThreshold;
            params.minRepeatGapSec = repeatGap;
            params.sameFileGapSec = sameFileGap;
            params.crossFileGapSec = crossFileGap;
            params.duplicateWindowSec = duplicateWindow;
            params.noiseFactor = noiseFactor;
            params.maxUniqueResults = static_cast<std::size_t>(maxUniqueResults);
            params.timeWeight = timeWeight;
            params.normalizeSize = normalizeSize;
            const bool appearanceReady = reidReady && std::any_of(windows.begin(), windows.end(),
                [](const auto& window) { return !window.appearanceEmbedding.empty(); });
            params.requireAppearance = appearanceReady;
            params.minAppearanceSimilarity = 0.55;
            params.appearanceWeight = 0.20;
            foundMatches = pfcore::MotionMatcher(params).findAllPairs(windows);
            pfcore::MotionRanker::rank(foundMatches, windows);
            matches = static_cast<int>(foundMatches.size());
            const QStringList windowPreviewA = previewA;
            const QStringList windowPreviewB = previewB;
            previewA.clear();
            previewB.clear();
            for (std::size_t i = 0; i < foundMatches.size(); ++i) {
                const auto& item = foundMatches[i];
                QVariantMap record;
                record.insert(QStringLiteral("id"), static_cast<int>(i));
                record.insert(QStringLiteral("similarity"), item.similarity);
                record.insert(QStringLiteral("direction"), QString::fromStdString(item.directionLabel));
                record.insert(QStringLiteral("gesture"), QString::fromStdString(item.gestureLabel));
                record.insert(QStringLiteral("leftSource"), QString::fromStdString(item.leftSourceId));
                record.insert(QStringLiteral("rightSource"), QString::fromStdString(item.rightSourceId));
                record.insert(QStringLiteral("appearanceSimilarity"), item.appearanceSimilarity);
                record.insert(QStringLiteral("identityVerified"), item.appearanceVerified);
                if (item.leftIndex < windows.size())
                    record.insert(QStringLiteral("leftTrackId"), static_cast<qulonglong>(windows[item.leftIndex].trackId));
                if (item.rightIndex < windows.size())
                    record.insert(QStringLiteral("rightTrackId"), static_cast<qulonglong>(windows[item.rightIndex].trackId));
                record.insert(QStringLiteral("leftStart"), item.leftStartSeconds);
                record.insert(QStringLiteral("leftEnd"), item.leftEndSeconds);
                record.insert(QStringLiteral("rightStart"), item.rightStartSeconds);
                record.insert(QStringLiteral("rightEnd"), item.rightEndSeconds);
                const double timelineDuration = std::max({item.leftStartSeconds, item.leftEndSeconds,
                                                           item.rightStartSeconds, item.rightEndSeconds,
                                                           0.001});
                record.insert(QStringLiteral("duration"), timelineDuration);
                record.insert(QStringLiteral("rankScore"), item.rankScore);
                QVariantList markers;
                markers << item.leftStartSeconds << item.leftEndSeconds
                        << item.rightStartSeconds << item.rightEndSeconds;
                record.insert(QStringLiteral("markers"), markers);
                resultRecords.push_back(record);
                const QString exactA = savePreviewAt(item.leftSourceId, item.leftStartSeconds,
                                                     QStringLiteral("%1_match_%2_a.png").arg(previewToken).arg(static_cast<int>(i)));
                const QString exactB = savePreviewAt(item.rightSourceId, item.rightStartSeconds,
                                                     QStringLiteral("%1_match_%2_b.png").arg(previewToken).arg(static_cast<int>(i)));
                previewA.push_back(exactA.isEmpty() && item.leftIndex < static_cast<std::size_t>(windowPreviewA.size())
                                       ? windowPreviewA.at(static_cast<int>(item.leftIndex)) : exactA);
                previewB.push_back(exactB.isEmpty() && item.rightIndex < static_cast<std::size_t>(windowPreviewB.size())
                                       ? windowPreviewB.at(static_cast<int>(item.rightIndex)) : exactB);
                record.insert(QStringLiteral("leftPreview"), previewA.back());
                record.insert(QStringLiteral("rightPreview"), previewB.back());
                resultRecords.back() = record;
            }
        }
        const QString reidStatus = reidReady
            ? QStringLiteral("ReID: включён")
            : (reidModelPresent
                ? QStringLiteral("ReID: отключён (%1)").arg(reidFailure.isEmpty()
                    ? QStringLiteral("ошибка модели") : reidFailure)
                : QStringLiteral("ReID: модель не найдена, pose-only режим"));
        QMetaObject::invokeMethod(this, [this, files, frames, duration, scenes, poseDetections, matches, resultRecords, foundMatches, error, processedFrames, totalFramesEstimate, sourceFps, reidStatus] {
            fileCount_ = files; frameCount_ = frames; durationSeconds_ = duration; sceneCount_ = scenes;
            poseDetectionCount_ = poseDetections;
            matchCount_ = matches;
            results_ = resultRecords;
            matches_ = foundMatches;
            sourceFps_ = sourceFps;
            emit summaryChanged();
            emit resultsChanged();
            analysisCompleted_ = error.isEmpty();
            emit analysisStateChanged();
            setProgress(1.0, error.isEmpty() ? QStringLiteral("Анализ завершён") : QStringLiteral("Анализ остановлен"), processedFrames, totalFramesEstimate);
            busy_ = false; emit busyChanged();
            setStatus(error.isEmpty() ? QStringLiteral("Анализ сцен завершён · ") + reidStatus
                                      : QStringLiteral("Анализ остановлен: ") + error);
        }, Qt::QueuedConnection);
        } catch (const std::exception& exception) {
            const QString message = QString::fromUtf8(exception.what());
            QMetaObject::invokeMethod(this, [this, message] {
                busy_ = false;
                analysisCompleted_ = false;
                emit analysisStateChanged();
                emit busyChanged();
                setProgress(0.0, QStringLiteral("Ошибка анализа"), 0, 0);
                setStatus(QStringLiteral("Анализ не запущен: ") + message);
            }, Qt::QueuedConnection);
        } catch (...) {
            QMetaObject::invokeMethod(this, [this] {
                busy_ = false;
                analysisCompleted_ = false;
                emit analysisStateChanged();
                emit busyChanged();
                setProgress(0.0, QStringLiteral("Ошибка анализа"), 0, 0);
                setStatus(QStringLiteral("Анализ не запущен: неизвестная ошибка"));
            }, Qt::QueuedConnection);
        }
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

} // namespace pfui
