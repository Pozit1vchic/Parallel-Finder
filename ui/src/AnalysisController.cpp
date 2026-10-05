#include "AnalysisController.h"
#include <QCryptographicHash>
#include "AppInfo.h"

#include <QMetaObject>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QStandardPaths>
#include <QSaveFile>
#include <QQmlEngine>
#include <QThread>
#include <QVariantMap>
#include <QVersionNumber>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <mutex>
#include <map>
#include <functional>
#include <unordered_map>
#include <vector>

#include "pfcore/VideoDecoder.hpp"
#include "pfcore/VideoSampleReader.hpp"
#include "pfcore/SceneDetector.hpp"
#include <unordered_set>
#include "pfcore/MotionMatcher.hpp"
#include "pfcore/ParallelClip.hpp"
#include "pfcore/MotionRanker.hpp"
#include "pfcore/DominantPerson.hpp"
#include "pfgpu/PoseEstimator.hpp"
#include "pfgpu/ReIdEstimator.hpp"
#include "pfgpu/FaceEstimator.hpp"
#include "pfgpu/DeviceInfo.hpp"
#include "pfservices/SettingsStore.hpp"
#include "pfservices/ModelStore.hpp"
#include "pfservices/PfCache.hpp"
#include "pfservices/CutService.hpp"
#include "pfservices/ExportQueue.hpp"
#include "pfservices/MontageExport.hpp"
#include "pfservices/RuntimeScratch.hpp"
#include "pfservices/PreviewMemo.hpp"
#include "pfservices/CachedPreview.hpp"
#include "pfservices/PreviewWriter.hpp"
#include "pfservices/PreviewImage.hpp"
#include <pfcore/PoseSupport.hpp>
#include "pfexporters/ExportOptions.hpp"

namespace pfui {
namespace {

constexpr auto kModelReleaseBase = "https://github.com/Pozit1vchic/Parallel-Finder/releases/download/v0.1.0-models/";
constexpr auto kModelManifestUrl = "https://github.com/Pozit1vchic/Parallel-Finder/releases/download/v0.1.0-models/manifest.json";

bool isSafeModelFilename(const QString& filename)
{
    return !filename.isEmpty() && filename.endsWith(QStringLiteral(".onnx"), Qt::CaseInsensitive)
        && !filename.contains(QStringLiteral(".."))
        && !filename.contains(QLatin1Char('/')) && !filename.contains(QLatin1Char('\\'));
}

bool isSafeExportPrefix(const QString& value)
{
    if (value.isEmpty() || value == QStringLiteral(".") || value == QStringLiteral("..")
        || value.size() > 64) return false;
    for (const QChar character : value) {
        if (!(character.isLetterOrNumber() || character == QLatin1Char('_')
              || character == QLatin1Char('-') || character == QLatin1Char('.'))) {
            return false;
        }
    }
    return true;
}

std::filesystem::path userModelsRoot()
{
    return std::filesystem::path(pfservices::SettingsStore::defaultDirectory()) / "models";
}

std::vector<std::filesystem::path> modelRoots()
{
    std::vector<std::filesystem::path> roots;
    // An explicit model root applies consistently to all three estimators,
    // including face models. This also keeps controlled cache comparisons
    // tied to the same model files when changing the executable location.
    if (const char* root = std::getenv("PF_MODEL_ROOT"); root && *root)
        roots.emplace_back(root);
    roots.emplace_back(std::filesystem::path(QCoreApplication::applicationDirPath().toStdWString()) / "models");
    roots.emplace_back(userModelsRoot());
    roots.emplace_back(R"(D:\PF_CUDA\models)");
    // Developer/download workspace used by the model preparation script. Keep
    // the historical typo as a compatibility fallback, but prefer the real
    // path so downloaded weights are visible without a PATH edit.
    roots.emplace_back(R"(D:\YOLO_Download_Project\models)");
    roots.emplace_back(R"(D:\YOLO\_Download_Project\models)");
    return roots;
}

struct EstimatorPool {
    std::mutex mutex;
    std::unordered_map<std::string, std::shared_ptr<pfgpu::PoseEstimator>> pose;
    std::unordered_map<std::string, std::shared_ptr<pfgpu::ReIdEstimator>> reid;
    std::unordered_map<std::string, std::shared_ptr<pfgpu::FaceEstimator>> face;
};

EstimatorPool& estimatorPool()
{
    static EstimatorPool pool;
    return pool;
}

std::string providerKey(const QString& providerChoice)
{
    return providerChoice.trimmed().isEmpty()
        ? std::string("auto")
        : providerChoice.trimmed().toLower().toStdString();
}

std::shared_ptr<pfgpu::PoseEstimator> sharedPoseEstimator(const std::filesystem::path& model,
                                                          const QString& providerChoice,
                                                          int processingThreads)
{
    pfgpu::PoseEstimatorParams params;
    if (const auto provider = pfgpu::parseProvider(providerChoice.toStdString()); provider.has_value())
        params.provider = *provider;
    params.intraOpThreads = processingThreads;
    const std::string modelName = model.filename().string();
    if (modelName.find("-b16") != std::string::npos) params.profile = "b16";
    else if (modelName.find("-b8") != std::string::npos) params.profile = "b8";
    else params.profile = "b1";
    const std::string key = model.string() + "|" + providerKey(providerChoice)
        + "|" + std::to_string(processingThreads) + "|" + params.profile;
    auto& pool = estimatorPool();
    const std::lock_guard<std::mutex> lock(pool.mutex);
    if (const auto it = pool.pose.find(key); it != pool.pose.end()) return it->second;
    auto estimator = std::make_shared<pfgpu::PoseEstimator>(model.string(), params);
    pool.pose.emplace(key, estimator);
    return estimator;
}

std::shared_ptr<pfgpu::ReIdEstimator> sharedReIdEstimator(const std::filesystem::path& model,
                                                          const QString& providerChoice,
                                                          int processingThreads)
{
    pfgpu::ReIdEstimatorParams params;
    if (const auto provider = pfgpu::parseProvider(providerChoice.toStdString()); provider.has_value())
        params.provider = *provider;
    params.intraOpThreads = processingThreads;
    const std::string key = model.string() + "|" + providerKey(providerChoice)
        + "|" + std::to_string(processingThreads);
    auto& pool = estimatorPool();
    const std::lock_guard<std::mutex> lock(pool.mutex);
    if (const auto it = pool.reid.find(key); it != pool.reid.end()) return it->second;
    auto estimator = std::make_shared<pfgpu::ReIdEstimator>(model.string(), params);
    pool.reid.emplace(key, estimator);
    return estimator;
}

std::shared_ptr<pfgpu::FaceEstimator> sharedFaceEstimator(const std::filesystem::path& detector,
                                                          const std::filesystem::path& recognizer,
                                                          const QString& providerChoice)
{
    const auto provider = pfgpu::parseProvider(providerChoice.toStdString()).value_or(pfgpu::Provider::Auto);
    const std::string key = detector.string() + "|" + recognizer.string() + "|"
        + providerKey(providerChoice);
    auto& pool = estimatorPool();
    const std::lock_guard<std::mutex> lock(pool.mutex);
    if (const auto it = pool.face.find(key); it != pool.face.end()) return it->second;
    auto estimator = std::make_shared<pfgpu::FaceEstimator>(detector.string(), recognizer.string(), provider);
    pool.face.emplace(key, estimator);
    return estimator;
}

std::optional<std::filesystem::path> findLocalModelFile(const QString& filename)
{
    if (!isSafeModelFilename(filename)) return std::nullopt;
    if (const char* configured = std::getenv("PF_MODEL_PATH"); configured && *configured) {
        const std::filesystem::path path(configured);
        if (QFileInfo(QString::fromLocal8Bit(configured)).fileName() == filename) {
            std::error_code error;
            if (std::filesystem::is_regular_file(path, error)) return path;
        }
    }
    std::string settingsError;
    const auto settings = pfservices::SettingsStore().load(settingsError);
    if (!settings.modelPath.empty()) {
        const std::filesystem::path path(settings.modelPath);
        if (QFileInfo(QString::fromStdString(settings.modelPath)).fileName() == filename) {
            std::error_code error;
            if (std::filesystem::is_regular_file(path, error)) return path;
        }
    }
    const auto requested = filename.toStdWString();
    for (const auto& root : modelRoots()) {
        const auto candidate = root / requested;
        std::error_code error;
        if (std::filesystem::is_regular_file(candidate, error)) return candidate;
    }
    return std::nullopt;
}

// Derived windows depend on more than a filename.  Reusing a cache entry after
// replacing an ONNX file in place or changing scene segmentation settings is a
// correctness bug: the interface appears to accept the new choice while it
// silently displays windows made by an older pipeline.
std::string cacheFileFingerprint(const std::filesystem::path& path)
{
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error) return path.string() + "|unavailable";
    const auto writeTime = std::filesystem::last_write_time(path, error);
    if (error) return path.string() + "|" + std::to_string(size) + "|time-unavailable";
    return path.string() + "|" + std::to_string(size) + "|"
        + std::to_string(writeTime.time_since_epoch().count());
}

std::optional<pfservices::ModelAsset> findLocalOrRemoteAsset(const QString& filename,
                                                             std::string& error)
{
    const std::string requested = filename.toStdString();
    for (const auto& root : modelRoots()) {
        const auto manifest = root / "manifest.json";
        std::string manifestError;
        if (auto asset = pfservices::ModelStore::readManifest(manifest, requested, manifestError)) {
            if (asset->downloadUrl.empty())
                asset->downloadUrl = std::string(kModelReleaseBase) + asset->filename;
            return asset;
        }
        if (error.empty()) error = manifestError;
    }
    std::string remoteError;
    if (auto asset = pfservices::ModelStore::fetchManifest(kModelManifestUrl,
                                                           requested,
                                                           remoteError)) {
        if (asset->downloadUrl.empty())
            asset->downloadUrl = std::string(kModelReleaseBase) + asset->filename;
        return asset;
    }
    // Never silently install an unverified release asset.  A missing or
    // unreachable manifest is an installation error, not permission to trust
    // arbitrary bytes at a predictable GitHub URL.
    error = remoteError.empty() ? error : remoteError;
    return std::nullopt;
}

bool modelVersionIsCompatible(const pfservices::ModelAsset& asset, QString& error)
{
    if (asset.minimumAppVersion.empty()) return true;
    const QVersionNumber required = QVersionNumber::fromString(
        QString::fromStdString(asset.minimumAppVersion));
    const QVersionNumber current = QVersionNumber::fromString(
        QCoreApplication::applicationVersion());
    if (required.isNull()) {
        error = QStringLiteral("В manifest указан некорректный minimumAppVersion: %1")
            .arg(QString::fromStdString(asset.minimumAppVersion));
        return false;
    }
    if (current.isNull() || QVersionNumber::compare(current, required) < 0) {
        error = QStringLiteral("Для модели нужна версия приложения %1 или новее; установлена %2")
            .arg(QString::fromStdString(asset.minimumAppVersion),
                 QCoreApplication::applicationVersion());
        return false;
    }
    return true;
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

QString previewSessionPath()
{
    static const pfservices::RuntimeScratch scratch(QStandardPaths::writableLocation(QStandardPaths::TempLocation)
        + QStringLiteral("/ParallelFinder/previews"));
    return scratch.path();
}

QString savePreview(const pfcore::DecodedFrame& frame, const QString& name)
{
    if (frame.rgba.empty() || frame.width <= 0 || frame.height <= 0) return {};
    const QString directory = previewSessionPath();
    if (directory.isEmpty()) return {};
    const QString path = directory + QLatin1Char('/') + name;
    QImage image(frame.rgba.data(), frame.width, frame.height, QImage::Format_RGBA8888);
    QImage preview = image; // borrowed source stays alive until synchronous encoding finishes
    // Keep enough source detail for a large A/B viewport. The old 960x540
    // cap was visibly soft on 1440p/4K footage after the image was enlarged
    // by PreserveAspectFit.
    if (preview.width() > 1920 || preview.height() > 1080)
        preview = preview.scaled(1920, 1080, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (!pfservices::saveLosslessPreview(preview, path)) return {};
    // Image.source is a URL in QML.  A bare Windows path such as C:/tmp/a.png
    // may be parsed as a URL with the scheme "c" and fail silently.
    return QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
}

QString savePreviewAt(pfcore::VideoDecoder& decoder, double timestamp, const QString& name,
    const std::function<QString(const pfcore::DecodedFrame&, const QString&)>& save = {})
{
    try {
        QElapsedTimer timer;
        timer.start();
        decoder.seek(std::max(0.0, timestamp));
        const auto seekMs = timer.restart();
        pfcore::DecodedFrame frame;
        const double target = std::max(0.0, timestamp);
        while (decoder.readNext(frame, false)) {
            if (frame.timestampSeconds + 1e-3 >= target) {
                if (!decoder.convertCurrentFrameToRgba(frame)) return {};
                const auto decodeMs = timer.restart();
                const auto url = save ? save(frame, name) : savePreview(frame, name);
                if (qEnvironmentVariableIsSet("PF_DEBUG_PREVIEW"))
                    std::fprintf(stderr, "PF_DEBUG_PREVIEW seek_ms=%lld decode_ms=%lld save_ms=%lld\n",
                        static_cast<long long>(seekMs), static_cast<long long>(decodeMs),
                        static_cast<long long>(timer.elapsed()));
                return url;
            }
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

// Pose and body-ReID do not benefit from 4K input: both models letterbox to
// their own small tensor (normally 640x640/256x128), while decoding and
// copying a 4K RGBA frame costs roughly eight times more memory bandwidth than
// a 720p frame. The analysis decoder is already capped at this working size;
// this helper also protects callers that provide a full-resolution frame. The
// preview path reopens the source separately when a full-resolution still is
// requested.
struct InferenceSample {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;
    const std::uint8_t* borrowed = nullptr;
    const std::uint8_t* pixels() const noexcept { return borrowed ? borrowed : (rgba.empty() ? nullptr : rgba.data()); }
};

InferenceSample makeInferenceSample(const pfcore::DecodedFrame& frame, bool allowBorrow = false)
{
    InferenceSample result;
    if (frame.width <= 0 || frame.height <= 0 || frame.rgba.empty()) return result;

    constexpr int kMaxInferenceWidth = 1280;
    constexpr int kMaxInferenceHeight = 720;
    const double scale = std::min({1.0,
                                   static_cast<double>(kMaxInferenceWidth) / frame.width,
                                   static_cast<double>(kMaxInferenceHeight) / frame.height});
    const int targetWidth = std::max(1, static_cast<int>(std::lround(frame.width * scale)));
    const int targetHeight = std::max(1, static_cast<int>(std::lround(frame.height * scale)));

    result.width = targetWidth;
    result.height = targetHeight;
    if (targetWidth == frame.width && targetHeight == frame.height) {
        // Batch-1 pose/face/ReID finish before the next decoded frame is read.
        // Borrow those already-sized, immutable bytes instead of allocating
        // and copying another working frame. Queued batches retain ownership.
        if (allowBorrow) result.borrowed = frame.rgba.data();
        else result.rgba = frame.rgba;
        return result;
    }
    QImage source(frame.rgba.data(), frame.width, frame.height,
                  frame.width * 4, QImage::Format_RGBA8888);
    QImage resized = source.scaled(targetWidth, targetHeight, Qt::IgnoreAspectRatio,
                                   Qt::FastTransformation)
                         .convertToFormat(QImage::Format_RGBA8888);
    const qsizetype bytes = resized.sizeInBytes();
    result.rgba.resize(static_cast<std::size_t>(std::max<qsizetype>(0, bytes)));
    if (bytes > 0 && resized.constBits())
        std::memcpy(result.rgba.data(), resized.constBits(), static_cast<std::size_t>(bytes));
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

std::vector<std::uint8_t> serializeMotionWindows(const std::vector<pfcore::MotionWindow>& windows, int sceneCount)
{
    std::vector<std::uint8_t> output;
    const std::uint32_t version = 9;
    appendBytes(output, version);
    appendBytes(output, static_cast<std::uint32_t>(windows.size()));
    appendBytes(output, static_cast<std::uint32_t>(sceneCount));
    for (const auto& window : windows) {
        appendBytes(output, static_cast<std::uint32_t>(window.sourceId.size()));
        output.insert(output.end(), window.sourceId.begin(), window.sourceId.end());
        appendBytes(output, static_cast<std::uint64_t>(window.trackId));
        appendBytes(output, static_cast<std::uint64_t>(window.sceneIndex));
        appendBytes(output, static_cast<std::uint8_t>(window.hasSceneIndex ? 1 : 0));
        appendBytes(output, static_cast<std::uint8_t>(window.staticFrameSet ? 1 : 0));
        appendBytes(output, window.sceneStartSeconds);
        appendBytes(output, window.sceneEndSeconds);
        appendBytes(output, window.appearanceConfidence);
        appendBytes(output, static_cast<std::uint32_t>(window.appearanceEmbedding.size()));
        for (const float value : window.appearanceEmbedding) appendBytes(output, value);
        appendBytes(output, static_cast<std::uint32_t>(window.sceneContext.size()));
        for (const float value : window.sceneContext) appendBytes(output, value);
        appendBytes(output, static_cast<std::uint8_t>(window.sceneViewSampled ? 1 : 0));
        appendBytes(output, static_cast<std::uint32_t>(window.sceneView.size()));
        for (const float value : window.sceneView) appendBytes(output, value);
        appendBytes(output, window.faceConfidence);
        appendBytes(output, static_cast<std::uint32_t>(window.faceEmbedding.size()));
        for (const float value : window.faceEmbedding) appendBytes(output, value);
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
                              std::vector<pfcore::MotionWindow>& windows, int& cachedSceneCount)
{
    cachedSceneCount = -1;
    std::size_t offset = 0;
    std::uint32_t version = 0, windowCount = 0;
    if (!readBytes(input, offset, version) || (version != 7 && version != 8 && version != 9)
        || !readBytes(input, offset, windowCount) || windowCount > 100'000U) return false;
    if (version >= 8) {
        std::uint32_t count = 0;
        if (!readBytes(input, offset, count) || count > 10'000'000U) return false;
        cachedSceneCount = static_cast<int>(count);
    }
    windows.clear();
    windows.reserve(windowCount);
    std::uint64_t totalFrames = 0;
    for (std::uint32_t windowIndex = 0; windowIndex < windowCount; ++windowIndex) {
        std::uint32_t sourceSize = 0;
        if (!readBytes(input, offset, sourceSize) || sourceSize > 16U * 1024U
            || offset + sourceSize > input.size()) return false;
        pfcore::MotionWindow window;
        window.sourceId.assign(reinterpret_cast<const char*>(input.data() + offset), sourceSize);
        offset += sourceSize;
        std::uint64_t trackId = 0, sceneIndex = 0;
        std::uint8_t hasSceneIndex = 0, staticFrameSet = 0;
        if (!readBytes(input, offset, trackId) || !readBytes(input, offset, sceneIndex)
            || !readBytes(input, offset, hasSceneIndex)
            || !readBytes(input, offset, staticFrameSet)) return false;
        window.trackId = static_cast<std::size_t>(trackId);
        window.sceneIndex = static_cast<std::size_t>(sceneIndex);
        window.hasSceneIndex = hasSceneIndex != 0;
        window.staticFrameSet = staticFrameSet != 0;
        if (!readBytes(input, offset, window.sceneStartSeconds)
            || !readBytes(input, offset, window.sceneEndSeconds)
            || !std::isfinite(window.sceneStartSeconds)
            || !std::isfinite(window.sceneEndSeconds)) return false;
        std::uint32_t embeddingSize = 0;
        if (!readBytes(input, offset, window.appearanceConfidence)
            || !std::isfinite(window.appearanceConfidence)
            || !readBytes(input, offset, embeddingSize)
            || embeddingSize > 4096U) return false;
        window.appearanceEmbedding.resize(embeddingSize);
        for (float& value : window.appearanceEmbedding) {
            if (!readBytes(input, offset, value) || !std::isfinite(value)) return false;
        }
        if (version >= 8) {
            std::uint32_t contextSize = 0;
            // A fully black/short shot legitimately has no histogram. Empty
            // context is missing evidence, not a corrupt or incomplete cache.
            if (!readBytes(input, offset, contextSize) || contextSize > 4096U) return false;
            window.sceneContext.resize(contextSize);
            for (auto& value : window.sceneContext)
                if (!readBytes(input, offset, value) || !std::isfinite(value) || value < 0.0F) return false;
        }
        if (version >= 9) {
            std::uint8_t sampled = 0; std::uint32_t viewSize = 0;
            if (!readBytes(input, offset, sampled) || sampled > 1
                || !readBytes(input, offset, viewSize) || (viewSize != 0 && viewSize != 432)) return false;
            window.sceneViewSampled = sampled != 0;
            window.sceneView.resize(viewSize);
            for (auto& value : window.sceneView)
                if (!readBytes(input, offset, value) || !std::isfinite(value) || value < 0 || value > 1) return false;
        }
        if (!readBytes(input, offset, window.faceConfidence) || !std::isfinite(window.faceConfidence)
            || !readBytes(input, offset, embeddingSize) || embeddingSize > 4096U) return false;
        window.faceEmbedding.resize(embeddingSize);
        for (float& value : window.faceEmbedding)
            if (!readBytes(input, offset, value) || !std::isfinite(value)) return false;
        std::uint32_t frameCount = 0;
        if (!readBytes(input, offset, frameCount) || frameCount > 100'000U
            || totalFrames > 2'000'000ULL - frameCount) return false;
        totalFrames += frameCount;
        window.frames.reserve(frameCount);
        for (std::uint32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
            pfcore::PoseFrame frame;
            std::uint32_t pointCount = 0;
            if (!readBytes(input, offset, frame.timestampSeconds)
                || !std::isfinite(frame.timestampSeconds)
                || !readBytes(input, offset, pointCount) || pointCount > 128U) return false;
            frame.keypoints.resize(pointCount);
            for (auto& point : frame.keypoints) {
                if (!readBytes(input, offset, point.x) || !readBytes(input, offset, point.y)
                    || !readBytes(input, offset, point.confidence)
                    || !std::isfinite(point.x) || !std::isfinite(point.y)
                    || !std::isfinite(point.confidence)) return false;
            }
            window.frames.push_back(std::move(frame));
        }
        windows.push_back(std::move(window));
    }
    return offset == input.size();
}

std::size_t ensureSceneViews(std::vector<pfcore::MotionWindow>& windows,
    pfcore::VideoDecoder& decoder, const std::function<bool()>& cancelled)
{
    std::map<std::size_t, std::size_t> representatives;
    std::unordered_map<std::size_t, std::vector<float>> sampled;
    for (std::size_t i = 0; i < windows.size(); ++i) {
        const auto& window = windows[i];
        if (!window.hasSceneIndex || window.frames.empty()) continue;
        if (window.sceneViewSampled) sampled.try_emplace(window.sceneIndex, window.sceneView);
        if (window.staticFrameSet) representatives.try_emplace(window.sceneIndex, i);
    }
    std::size_t decodedViews = 0;
    decoder.setRgbaMaxDimensions(160, 90);
    for (const auto& [scene, index] : representatives) {
        if (cancelled()) break;
        if (sampled.contains(scene)) continue;
        const auto& window = windows[index];
        const double target = (window.frames.front().timestampSeconds + window.frames.back().timestampSeconds) / 2;
        decoder.seek(target);
        pfcore::DecodedFrame frame;
        std::vector<float> view;
        while (!cancelled() && decoder.readNext(frame, false)) {
            if (frame.timestampSeconds + 1e-6 < target) continue;
            if (frame.timestampSeconds <= window.sceneEndSeconds + 1e-6
                && decoder.convertCurrentFrameToRgba(frame)) {
                view = pfcore::sceneViewDescriptor({frame.timestampSeconds, frame.width, frame.height, frame.rgba});
                ++decodedViews;
            }
            break;
        }
        if (!cancelled()) sampled.emplace(scene, std::move(view));
    }
    decoder.setRgbaMaxDimensions(1280, 720);
    for (auto& window : windows) if (const auto found = sampled.find(window.sceneIndex); found != sampled.end()) {
        window.sceneView = found->second;
        window.sceneViewSampled = true;
    }
    return decodedViews;
}

void ensureSceneSequences(std::vector<pfcore::MotionWindow>& windows,
    const std::string& source,const std::string& cacheKey,pfservices::PfCache* cache,
    bool preferNvidia,const std::function<bool()>& cancelled,
    const std::vector<pfcore::SceneSample>* observedSamples=nullptr)
{
    const auto key=std::string("scene-sequence-v5|")+QCryptographicHash::hash(
        QByteArray::fromStdString(cacheKey),QCryptographicHash::Sha256).toHex().toStdString();
    if(cache) {std::string ownerError;cache->rememberSourceKey(source,key,ownerError);}
    QJsonObject saved;
    if (cache) if (const auto bytes=cache->get(key)) {
        const auto doc=QJsonDocument::fromJson(QByteArray(reinterpret_cast<const char*>(bytes->data()),bytes->size()));
        if (doc.isObject()) saved=doc.object();
    }
    std::map<std::size_t,std::size_t> shots;
    for (std::size_t i=0;i<windows.size();++i)
        if (windows[i].sourceId==source && windows[i].hasSceneIndex) shots.try_emplace(windows[i].sceneIndex,i);
    struct Request {double time;std::size_t scene;};
    std::vector<Request> requests;
    std::map<std::size_t,std::pair<QJsonArray,QJsonArray>> pending;
    std::size_t decoded=0,reused=0;bool changed=false;
    for (const auto& [scene,index]:shots) {
        if (cancelled()) return;
        auto& w=windows[index];const QString id=QString::number(scene);
        w.sceneSequence.clear();w.sceneSequenceTimes.clear();
        const auto entry=saved.value(id).toObject();
        const auto pixels=entry.value("pixels").toArray(),pts=entry.value("pts").toArray();
        const bool valid=(pixels.isEmpty() || (pixels.size()>=1296 && pixels.size()%432==0))
            && pts.size()==pixels.size()/432
            && entry.value("start").toDouble(-1)==w.sceneStartSeconds
            && entry.value("end").toDouble(-1)==w.sceneEndSeconds
            && std::all_of(pixels.begin(),pixels.end(),[](const auto& p){return p.isDouble()
                && std::isfinite(p.toDouble()) && p.toDouble()>=0 && p.toDouble()<=1;})
            && std::all_of(pts.begin(),pts.end(),[&w](const auto& p){return p.isDouble()
                && std::isfinite(p.toDouble()) && p.toDouble()>=w.sceneStartSeconds && p.toDouble()<w.sceneEndSeconds;})
            && (pixels.isEmpty()==pts.isEmpty());
        if (saved.contains(id) && valid) {
            for (const auto p:pixels)w.sceneSequence.push_back(p.toDouble());
            for (const auto p:pts)w.sceneSequenceTimes.push_back(p.toDouble());
            continue;
        }
        pending.try_emplace(scene);changed=true;
        const double span=w.sceneEndSeconds-w.sceneStartSeconds;
        if (span>=.50 && w.sceneStartSeconds>=0)
            for (double t=w.sceneStartSeconds+.05;t<w.sceneEndSeconds-.05;t+=.25) requests.push_back({t,scene});
    }
    std::sort(requests.begin(),requests.end(),[](const Request& a,const Request& b){return a.time<b.time;});
    const auto record=[&](const Request& request,const pfcore::SceneSample& sample) {
        const auto& w=windows[shots.at(request.scene)];
        if (sample.timestampSeconds<w.sceneStartSeconds || sample.timestampSeconds>=w.sceneEndSeconds
            || std::abs(sample.timestampSeconds-request.time)>.15) return;
        const auto view=pfcore::sceneContentDescriptor(sample);
        if (view.size()!=432) return;
        auto& [sequence,times]=pending.at(request.scene);
        for (const auto v:view)sequence.append(v);
        times.append(sample.timestampSeconds);
    };
    if (observedSamples) {
        // These are actual source frames already decoded for scene detection.
        // Use the closest observation, never resampled/interpolated pixels.
        for (const auto& request:requests) {
            if (cancelled()) return;
            auto found=std::lower_bound(observedSamples->begin(),observedSamples->end(),request.time,
                [](const auto& sample,double t){return sample.timestampSeconds<t;});
            if (found!=observedSamples->begin() && (found==observedSamples->end()
                || request.time-std::prev(found)->timestampSeconds<found->timestampSeconds-request.time)) --found;
            if (found!=observedSamples->end()) {record(request,*found);++reused;}
        }
    } else if (!requests.empty()) {
        // Old window caches do not contain thumbnails. Migrate with ONE
        // forward traversal instead of reconstructing a long GOP per sample.
        pfcore::VideoDecoder decoder;pfcore::VideoDecodeOptions options;
        options.threads=8;options.preferNvidia=preferNvidia;options.minimumNvidiaPixels=1920ULL*1080ULL+1;
        decoder.open(source,options);decoder.setRgbaMaxDimensions(160,90);
        decoder.seek(requests.front().time);pfcore::DecodedFrame frame;bool available=false;
        for (const auto& request:requests) {
            if (cancelled()) return;
            while ((!available || frame.timestampSeconds+1e-6<request.time) && decoder.readNext(frame,false)) available=true;
            if (!available || frame.timestampSeconds+1e-6<request.time) break;
            if (decoder.convertCurrentFrameToRgba(frame)) {
                record(request,{frame.timestampSeconds,frame.width,frame.height,frame.rgba});++decoded;
            }
        }
    }
    for (auto& [scene,measurements]:pending) {
        auto& w=windows[shots.at(scene)];auto& [sequence,times]=measurements;
        if (sequence.size()<1296 || sequence.size()!=times.size()*432) {sequence={};times={};}
        for (const auto p:sequence)w.sceneSequence.push_back(p.toDouble());
        for (const auto p:times)w.sceneSequenceTimes.push_back(p.toDouble());
        saved.insert(QString::number(scene),QJsonObject{{"start",w.sceneStartSeconds},{"end",w.sceneEndSeconds},
            {"pixels",sequence},{"pts",times}});
    }
    // Keep one measured representative per shot. Aliases are derived from
    // it before retrieval; sliding windows do not duplicate the thumbnails.
    if (cache && changed && !cancelled()) {
        const auto bytes=QJsonDocument(saved).toJson(QJsonDocument::Compact);std::string error;
        cache->putForSource(source,key,std::vector<std::uint8_t>(bytes.begin(),bytes.end()),error);
    }
    if (qEnvironmentVariableIsSet("PF_DEBUG_ANALYSIS"))
        std::fprintf(stderr,"PF_DEBUG_SEQUENCE source=%s shots=%zu sampled_frames=%zu reused_scene_samples=%zu\n",source.c_str(),shots.size(),decoded,reused);
}

std::vector<float> sourceFaceCentroid(const std::vector<pfcore::MotionWindow>& windows,const std::string& source)
{
    std::map<std::size_t,const pfcore::MotionWindow*> anchors;
    for(const auto& w:windows) {
        double norm=0;for(const float f:w.faceEmbedding)norm+=f*f;
        if(w.sourceId!=source || w.faceConfidence<.45 || !std::isfinite(norm) || norm<=1e-12)continue;
        auto& slot=anchors[w.sceneIndex];if(!slot || w.faceConfidence>slot->faceConfidence)slot=&w;
    }
    std::vector<float> face;
    for(const auto& [_,w]:anchors) {
        double norm=0;for(const float f:w->faceEmbedding)norm+=f*f;
        if(face.empty())face.assign(w->faceEmbedding.size(),0);
        if(face.size()!=w->faceEmbedding.size())continue;
        for(std::size_t i=0;i<face.size();++i)face[i]+=w->faceEmbedding[i]/std::sqrt(norm);
    }
    return face;
}

std::string matchedFaceCacheKey(const std::string& key,const std::vector<float>& face)
{
    const auto bytes=QByteArray(reinterpret_cast<const char*>(face.data()),face.size()*sizeof(float));
    return "matched-face-v7|"+QCryptographicHash::hash(QByteArray::fromStdString(key)+bytes,QCryptographicHash::Sha256).toHex().toStdString();
}

class MatchedFaceVerifier {
    struct Source {
        std::vector<float> face;
        QJsonObject notes;
        std::string key;
        bool dirty=false;
    };
    std::map<std::string,Source> sources_;
    pfservices::PfCache* cache_;
    std::shared_ptr<pfgpu::FaceEstimator> estimator_;
    bool preferNvidia_;
    std::function<bool()> cancelled_;
    static double cosine(const std::vector<float>& a,const std::vector<float>& b) {
        if (a.empty() || a.size()!=b.size())return 0;
        double dot=0,x=0,y=0;for (std::size_t i=0;i<a.size();++i) {dot+=a[i]*b[i];x+=a[i]*a[i];y+=b[i]*b[i];}
        return x>0 && y>0 ? dot/std::sqrt(x*y) : 0;
    }
    static QString noteKey(std::size_t track,double time) {
        return QString::number(track)+"|"+QString::number(std::llround(time*1e6));
    }
    static bool add(pfcore::MotionWindow& w,const QJsonObject& o) {
        const double time=o["time"].toDouble(-1);
        if (!std::isfinite(time) || w.frames.empty() || time<w.frames.front().timestampSeconds-.01
            || time>w.frames.back().timestampSeconds+.01
            || o["track"].toInteger()!=static_cast<qint64>(w.trackId))return false;
        if (std::any_of(w.measuredFaces.begin(),w.measuredFaces.end(),[&](const auto& f){return std::abs(f.timestampSeconds-time)<1e-6;}))return false;
        const double similarity=o["similarity"].toDouble(),eye=o["eyeSpan"].toDouble();
        if (!o["observed"].isBool() || !std::isfinite(similarity) || similarity<-1 || similarity>1
            || !std::isfinite(eye) || eye<0 || eye>1)return false;
        w.measuredFaces.push_back({time,o["observed"].toBool(),similarity,eye});return true;
    }
public:
    MatchedFaceVerifier(std::vector<pfcore::MotionWindow>& windows,
        const std::unordered_map<std::string,std::string>& keys,pfservices::PfCache* cache,
        std::shared_ptr<pfgpu::FaceEstimator> estimator,bool nvidia,std::function<bool()> cancelled)
        :cache_(cache),estimator_(std::move(estimator)),preferNvidia_(nvidia),cancelled_(std::move(cancelled)) {
        if (!estimator_)return;
        std::map<std::string,std::map<std::size_t,const pfcore::MotionWindow*>> anchors;
        for (const auto& w:windows) if (w.faceConfidence>=.45 && cosine(w.faceEmbedding,w.faceEmbedding)>.99) {
            auto& slot=anchors[w.sourceId][w.sceneIndex];if (!slot || w.faceConfidence>slot->faceConfidence)slot=&w;
        }
        for (const auto& [source,shots]:anchors) {
            auto& s=sources_[source];
            s.face=sourceFaceCentroid(windows,source);
            s.key=matchedFaceCacheKey(keys.at(source),s.face);
            if(cache_) {std::string ownerError;cache_->rememberSourceKey(source,s.key,ownerError);}
            if (cache_)if(const auto data=cache_->get(s.key)) {
                const auto doc=QJsonDocument::fromJson(QByteArray(reinterpret_cast<const char*>(data->data()),data->size()));
                if(doc.isObject())s.notes=doc.object();
            }
        }
        for (auto& w:windows)if(const auto it=sources_.find(w.sourceId);it!=sources_.end())
            for(const auto& note:it->second.notes)add(w,note.toObject());
    }
    std::size_t verify(std::vector<pfcore::MotionWindow>& windows,const std::vector<pfcore::MotionMatch>& matches) {
        if (!estimator_)return 0;
        struct Request {std::size_t track;pfcore::PoseFrame frame;};
        std::map<std::string,std::map<QString,Request>> requested;
        for (const auto& match:matches)for(const bool left:{true,false}) {
            const auto& w=windows[left?match.leftIndex:match.rightIndex];
            if(!sources_.contains(w.sourceId))continue;
            const double start=left?match.leftStartSeconds:match.rightStartSeconds,end=left?match.leftEndSeconds:match.rightEndSeconds;
            for(const double t:{start,start+(end-start)*.25,(start+end)/2,start+(end-start)*.75,end}) {
                const pfcore::PoseFrame* closest=nullptr;
                for(const auto& f:w.frames)if(f.timestampSeconds>=start-.001 && f.timestampSeconds<=end+.001
                    && (!closest || std::abs(f.timestampSeconds-t)<std::abs(closest->timestampSeconds-t)))closest=&f;
                if(closest)requested[w.sourceId].try_emplace(noteKey(w.trackId,closest->timestampSeconds),Request{w.trackId,*closest});
            }
        }
        std::size_t measured=0,added=0;
        for(auto& [source,requests]:requested) {
            auto& s=sources_.at(source);
            std::vector<std::pair<QString,Request>> pending;
            for(const auto& entry:requests)if(!s.notes.contains(entry.first))pending.push_back(entry);
            std::sort(pending.begin(),pending.end(),[](const auto& a,const auto& b){return a.second.frame.timestampSeconds<b.second.frame.timestampSeconds;});
            if(!pending.empty()) {
                pfcore::VideoDecoder decoder;pfcore::VideoDecodeOptions options;options.threads=8;
                options.preferNvidia=preferNvidia_;options.minimumNvidiaPixels=1920ULL*1080ULL+1;
                decoder.open(source,options);decoder.setRgbaMaxDimensions(1280,720);
                pfcore::DecodedFrame image;bool available=false;
                for(const auto& [key,request]:pending) {
                    if(cancelled_())return added;
                    const double target=request.frame.timestampSeconds;
                    if(!available || image.timestampSeconds>target+.01 || target-image.timestampSeconds>1) {
                        decoder.seek(target);available=false;
                    }
                    while(!available || image.timestampSeconds+.001<target) {
                        image.rgba.clear();if(!decoder.readNext(image,false))break;available=true;
                    }
                    QJsonObject note{{"track",static_cast<qint64>(request.track)},{"time",target},{"observed",false},{"similarity",0.0},{"eyeSpan",0.0}};
                    if(available && std::abs(image.timestampSeconds-target)<=.1
                        && (!image.rgba.empty() || decoder.convertCurrentFrameToRgba(image))) {
                        double l=image.width,r=0,t=image.height,b=0;
                        for(const auto& p:request.frame.keypoints)if(p.confidence>=.25 && std::isfinite(p.x+p.y)) {
                            l=std::min(l,p.x);r=std::max(r,p.x);t=std::min(t,p.y);b=std::max(b,p.y);
                        }
                        // The tracked face is more precise than a body crop
                        // containing several people. Never let a bystander's
                        // face veto this person because an arm widens the box.
                        double hl=image.width,hr=0,ht=image.height,hb=0;std::size_t visibleHead=0;
                        for(std::size_t i=0;i<std::min<std::size_t>(5,request.frame.keypoints.size());++i) {
                            const auto& p=request.frame.keypoints[i];
                            if(p.confidence<.5 || !std::isfinite(p.x+p.y))continue;
                            hl=std::min(hl,p.x);hr=std::max(hr,p.x);ht=std::min(ht,p.y);hb=std::max(hb,p.y);++visibleHead;
                        }
                        const bool tightHead=visibleHead>=3 && hr-hl>=16;
                        if(tightHead) {
                            const double size=std::max(hr-hl,(hb-ht)*1.5),cx=(hl+hr)/2;
                            l=cx-size*.8;r=cx+size*.8;t=ht-size*.35;b=hb+size*.65;
                        }
                        if(r>=l && b>=t) {
                            const double width=std::max(64.0,r-l),height=std::max(96.0,b-t),center=(l+r)/2;
                            const double x0=std::clamp(tightHead?l:center-width*.65,0.0,static_cast<double>(image.width));
                            const double x1=std::clamp(tightHead?r:center+width*.65,0.0,static_cast<double>(image.width));
                            const double y0=std::clamp(tightHead?t:t-height*.20,0.0,static_cast<double>(image.height));
                            const double y1=std::clamp(tightHead?b:t+height*1.15,0.0,static_cast<double>(image.height));
                            pfgpu::FaceRecognitionDiagnostics diagnostic;
                            std::array<double,2> nose{};const std::array<double,2>* hint=nullptr;
                            if(tightHead && !request.frame.keypoints.empty() && request.frame.keypoints[0].confidence>=.5) {
                                nose={request.frame.keypoints[0].x,request.frame.keypoints[0].y};hint=&nose;
                            }
                            // A back view has no measured nose to associate a
                            // detected face with this person. A bystander or
                            // a billboard must not veto its body ReID proof.
                            auto face=hint?estimator_->infer({image.width,image.height,image.rgba.data(),static_cast<float>(x0),static_cast<float>(y0),static_cast<float>(x1),static_cast<float>(y1)},&diagnostic,.85,hint):std::vector<float>{};
                            if(face.empty() && hint) {
                                // A single measured low-light crop gets a
                                // bounded exposure retry. Source pixels/PTS
                                // stay unchanged; ambiguous faces still fail.
                                auto exposed=image.rgba;
                                for(int y=static_cast<int>(y0);y<static_cast<int>(y1);++y)
                                    for(int x=static_cast<int>(x0);x<static_cast<int>(x1);++x)
                                        for(int c=0;c<3;++c) {
                                            auto& value=exposed[(static_cast<std::size_t>(y)*image.width+x)*4+c];
                                            value=static_cast<std::uint8_t>(std::min(255.0,std::sqrt(value/255.0)*255));
                                        }
                                face=estimator_->infer({image.width,image.height,exposed.data(),static_cast<float>(x0),static_cast<float>(y0),static_cast<float>(x1),static_cast<float>(y1)},&diagnostic,.65,hint);
                                // A weak detection can confirm the known
                                // person; it cannot veto a body as foreign.
                                if(diagnostic.detectorScore<.85 && cosine(face,s.face)<.363)face.clear();
                            }
                            if(face.empty() && hint && request.frame.keypoints.size()>=3) {
                                const auto& points=request.frame.keypoints;
                                // A profile may evade YuNet. Use only measured
                                // eye/nose anchors and recognize actual pixels;
                                // this fallback can confirm, never reject,
                                // the independently admitted source identity.
                                if(points[0].confidence>=.75 && points[1].confidence>=.75 && points[2].confidence>=.75) {
                                    const std::array<double,6> anchors{points[2].x,points[2].y,points[1].x,points[1].y,points[0].x,points[0].y};
                                    face=estimator_->inferFromPose({image.width,image.height,image.rgba.data(),static_cast<float>(x0),static_cast<float>(y0),static_cast<float>(x1),static_cast<float>(y1)},anchors,&diagnostic);
                                    if(cosine(face,s.face)<.43)face.clear();
                                }
                            }
                            const auto similarity=std::clamp(cosine(face,s.face),-1.0,1.0);
                            double eye=face.empty()?0:std::hypot(diagnostic.landmarks[0]-diagnostic.landmarks[2],diagnostic.landmarks[1]-diagnostic.landmarks[3])/image.width;
                            if(face.empty() && request.frame.keypoints.size()>=3) {
                                const auto& a=request.frame.keypoints[1];const auto& b=request.frame.keypoints[2];
                                if(a.confidence>=.5 && b.confidence>=.5 && std::isfinite(a.x+a.y+b.x+b.y))
                                    eye=std::hypot(a.x-b.x,a.y-b.y)/image.width;
                            }
                            note["observed"]=!face.empty();note["similarity"]=similarity;note["eyeSpan"]=eye;
                            note["pts"]=image.timestampSeconds;note["box"]=QJsonArray{x0,y0,x1,y1};
                        }
                    }
                    s.notes.insert(key,note);s.dirty=true;++measured;
                }
            }
            for(auto& w:windows)if(w.sourceId==source)
                for(const auto& [key,_]:requests)added+=add(w,s.notes.value(key).toObject());
            if(cache_ && s.dirty && !cancelled_()) {
                const auto data=QJsonDocument(s.notes).toJson(QJsonDocument::Compact);std::string error;
                cache_->putForSource(source,s.key,std::vector<std::uint8_t>(data.begin(),data.end()),error);s.dirty=false;
            }
        }
        if(qEnvironmentVariableIsSet("PF_DEBUG_ANALYSIS"))std::fprintf(stderr,"PF_DEBUG_MATCHED_FACES measured_frames=%zu attached_observations=%zu\n",measured,added);
        return added;
    }
};

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
            for (const auto& root : modelRoots()) {
                const auto candidate = root / requestedName;
                std::error_code filesystemError;
                if (std::filesystem::is_regular_file(candidate, filesystemError)) return candidate;
            }
        }
    }
    // Legacy developer location remains a fallback only. It must never
    // override a model explicitly selected in settings.json.
    // This is the universal batch-1 asset published in the model Release.
    // Do not default to a developer-only `-b1` filename: a fresh install must
    // be able to download exactly the model selected by default.
    const auto defaultName = QStringLiteral("yolo26m-pose.onnx");
    for (const auto& root : modelRoots()) {
        const auto candidate = root / defaultName.toStdWString();
        std::error_code filesystemError;
        if (std::filesystem::is_regular_file(candidate, filesystemError)) return candidate;
    }
    const auto localModels = userModelsRoot() / defaultName.toStdWString();
    for (const auto& root : modelRoots()) {
        const auto manifest = root / "manifest.json";
        std::string manifestError;
        auto asset = pfservices::ModelStore::readManifest(manifest,
            defaultName.toStdString(), manifestError);
        if (!asset.has_value()) continue;
        if (asset->downloadUrl.empty())
            asset->downloadUrl = std::string(kModelReleaseBase) + asset->filename;
        std::string downloadError;
        if (pfservices::ModelStore::download(*asset, localModels, {}, downloadError))
            return localModels;
    }
    std::string remoteManifestError;
    if (auto asset = pfservices::ModelStore::fetchManifest(kModelManifestUrl,
                                                            defaultName.toStdString(),
                                                            remoteManifestError)) {
        if (asset->downloadUrl.empty())
            asset->downloadUrl = std::string(kModelReleaseBase) + asset->filename;
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
    roots.emplace_back(R"(D:\YOLO_Download_Project\models)");
    roots.emplace_back(R"(D:\YOLO\_Download_Project\models)");
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
    // The canonical release name is also eligible for a one-time remote
    // lookup.  Do not probe five names in a row: a missing release manifest
    // must fail fast rather than stall the first analysis for every alias.
    {
        std::string remoteError;
        const auto canonical = preferred.front();
        if (auto asset = pfservices::ModelStore::fetchManifest(kModelManifestUrl,
                                                                canonical,
                                                                remoteError)) {
            std::string downloadError;
            const auto destination = localModels / canonical;
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
                                      double endSeconds, bool face = false)
{
    std::vector<float> sum;
    std::size_t samples = 0;
    const auto first = std::lower_bound(track.observations.begin(), track.observations.end(), startSeconds,
        [](const auto& observation, double time) { return observation.timestampSeconds < time; });
    for (auto current = first; current != track.observations.end()
         && current->timestampSeconds < endSeconds; ++current) {
        const auto& observation = *current;
        const auto& embedding = face ? observation.faceEmbedding : observation.appearanceEmbedding;
        if (embedding.empty()) continue;
        if (sum.empty()) sum.assign(embedding.size(), 0.0F);
        if (sum.size() != embedding.size()) continue;
        for (std::size_t index = 0; index < sum.size(); ++index)
            sum[index] += embedding[index];
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
    // ReID is intentionally sampled much less often than pose inference.
    // Treating its evidence as `reid samples / pose observations` made a
    // healthy fast run look unreliable (for example, 3 ReID crops among 18
    // pose frames scored 0.17) and removed every window before matching.
    // Three independently sampled, valid crops form a full prototype; one
    // crop is deliberately still below the matcher's evidence threshold.
    return {std::move(sum), std::min(1.0, static_cast<double>(samples) / 3.0)};
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
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this] {
        exportWorker_.request_stop();
        if (exportWorker_.joinable()) exportWorker_.join();
    });
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
    // Settings written by releases before the temporal matcher calibration
    // used .85/.55 (or .78/.50) as the balanced preset. Migrate those exact
    // profiles once so an existing installation does not keep thresholds
    // calibrated against the old, appearance-inflated score.
    const bool legacyBalanced =
        ((std::abs(similarityThreshold_ - 0.85) < 1e-6
          && std::abs(candidateThreshold_ - 0.55) < 1e-6)
         || (std::abs(similarityThreshold_ - 0.78) < 1e-6
             && std::abs(candidateThreshold_ - 0.50) < 1e-6))
        && std::abs(repeatGap_ - 6.0) < 1e-6
        && std::abs(sameFileGap_ - 2.0) < 1e-6
        && std::abs(duplicateWindow_ - 1.5) < 1e-6
        && std::abs(noiseFactor_ - 1.0) < 1e-6
        && maxUniqueResults_ == 100
        && std::abs(timeWeight_ - 0.25) < 1e-6;
    if (legacyBalanced) {
        similarityThreshold_ = 0.76;
        candidateThreshold_ = 0.55;
    }
    // Scores are now motion-only (ReID is an identity gate), so migrate the
    // old presets that were tuned against the inflated appearance score.
    const bool legacyFast =
        ((std::abs(similarityThreshold_ - 0.72) < 1e-6
          && std::abs(candidateThreshold_ - 0.40) < 1e-6)
         || (std::abs(similarityThreshold_ - 0.65) < 1e-6
             && std::abs(candidateThreshold_ - 0.45) < 1e-6))
        && std::abs(repeatGap_ - 8.0) < 1e-6
        && std::abs(sameFileGap_ - 3.0) < 1e-6
        && maxUniqueResults_ == 50;
    if (legacyFast) {
        similarityThreshold_ = 0.70;
        candidateThreshold_ = 0.48;
    }
    const bool legacyPrecise = std::abs(similarityThreshold_ - 0.65) < 1e-6
        && std::abs(candidateThreshold_ - 0.50) < 1e-6
        && std::abs(repeatGap_ - 4.0) < 1e-6
        && std::abs(sameFileGap_ - 1.5) < 1e-6
        && std::abs(duplicateWindow_ - 1.0) < 1e-6
        && std::abs(noiseFactor_ - 0.70) < 1e-6
        && maxUniqueResults_ == 200
        && std::abs(timeWeight_ - 0.40) < 1e-6;
    if (legacyPrecise) {
        similarityThreshold_ = 0.84;
        candidateThreshold_ = 0.68;
    }
    providerChoice_ = QString::fromStdString(settings.provider);
    // Keep headless diagnostics explicit without changing the persisted
    // provider selected in the UI.  This is also useful on machines where
    // only the CPU runtime is installed.
    const QString envProvider = qEnvironmentVariable("PF_PROVIDER").toLower();
    if (envProvider == QStringLiteral("auto") || envProvider == QStringLiteral("cpu")
        || envProvider == QStringLiteral("dml") || envProvider == QStringLiteral("cuda")
        || envProvider == QStringLiteral("tensorrt"))
        providerChoice_ = envProvider;
    qualityProfile_ = QStringLiteral("maximum");
    if (settings.qualityProfile == "fast" || settings.qualityProfile == "medium" || settings.qualityProfile == "maximum")
        qualityProfile_ = QString::fromStdString(settings.qualityProfile);
    const QString envQuality = qEnvironmentVariable("PF_QUALITY_PROFILE").toLower();
    if (envQuality == QStringLiteral("fast") || envQuality == QStringLiteral("medium")
        || envQuality == QStringLiteral("maximum"))
        qualityProfile_ = envQuality;
    if (settings.analysisMode == "motion" || settings.analysisMode == "static"
        || settings.analysisMode == "combined")
        analysisMode_ = QString::fromStdString(settings.analysisMode);
    // Headless benchmark/smoke runs can select a mode without mutating the
    // user's persisted UI preferences.  The normal application path leaves
    // this unset and uses the saved sidebar choice.
    const QString envMode = qEnvironmentVariable("PF_ANALYSIS_MODE");
    if (envMode == QStringLiteral("motion") || envMode == QStringLiteral("static")
        || envMode == QStringLiteral("combined"))
        analysisMode_ = envMode;
    normalizeSize_ = settings.normalizeSize;
    mirrorPoses_ = settings.mirrorPoses;
    expandedSearch_ = settings.expandedSearch;
    if (qEnvironmentVariableIsSet("PF_EXPANDED_SEARCH"))
        expandedSearch_ = qEnvironmentVariableIntValue("PF_EXPANDED_SEARCH") != 0;
    modelPath_ = QString::fromStdString(settings.modelPath);
    modelChoice_ = QFileInfo(QString::fromStdString(settings.modelChoice)).fileName();
    if (modelChoice_.isEmpty()) {
        // Legacy settings stored only a path. An unavailable old model is not
        // a meaningful default on a fresh install or after cache cleanup.
        const QString legacyName = QFileInfo(modelPath_).fileName();
        modelChoice_ = !legacyName.isEmpty() && findLocalModelFile(legacyName).has_value()
            ? legacyName : QStringLiteral("yolo26m-pose.onnx");
    }
    // Migrate the old developer-only default.  It was never present in the
    // public manifest, so leaving it selected on a clean update would make
    // the download control point at an unavailable asset.
    if (modelChoice_ == QStringLiteral("yolo26m-pose-640-b1.onnx")
        && !findLocalModelFile(modelChoice_).has_value()) {
        modelChoice_ = QStringLiteral("yolo26m-pose.onnx");
        modelPath_.clear();
    }
    modelStatus_ = findLocalModelFile(modelChoice_).has_value()
        ? QStringLiteral("Модель установлена и готова к анализу")
        : QStringLiteral("Модель не установлена · выберите её для скачивания");
    cachePath_ = QString::fromStdString(settings.cachePath);
    cacheLimitGb_ = static_cast<double>(settings.cacheLimitBytes) / (1024.0 * 1024.0 * 1024.0);
    processingThreads_ = static_cast<int>(std::clamp<std::size_t>(settings.processingThreads, 0, 256));
    sceneThreshold_ = settings.sceneThreshold;
    const auto near = [](double left, double right) { return std::abs(left - right) < 1e-6; };
    if (near(similarityThreshold_, 0.70) && near(candidateThreshold_, 0.48)
        && near(repeatGap_, 8.0) && near(sameFileGap_, 3.0)
        && near(duplicateWindow_, 2.0) && near(noiseFactor_, 1.25)
        && (maxUniqueResults_ == 50 || maxUniqueResults_ == 500) && near(timeWeight_, 0.10)) {
        accuracyPreset_ = QStringLiteral("fast");
        maxUniqueResults_=500;
    } else if (near(similarityThreshold_, 0.84) && near(candidateThreshold_, 0.68)
        && near(repeatGap_, 4.0) && near(sameFileGap_, 1.5)
        && near(duplicateWindow_, 1.0) && near(noiseFactor_, 0.70)
        && (maxUniqueResults_ == 200 || maxUniqueResults_ == 500) && near(timeWeight_, 0.40)) {
        accuracyPreset_ = QStringLiteral("precise");
        maxUniqueResults_=500;
    } else if (!near(similarityThreshold_, 0.76) || !near(candidateThreshold_, 0.55)
        || !near(repeatGap_, 6.0) || !near(sameFileGap_, 2.0)
        || !near(duplicateWindow_, 1.5) || !near(noiseFactor_, 1.0)
        || (maxUniqueResults_ != 100 && maxUniqueResults_ != 500) || !near(timeWeight_, 0.25)) {
        accuracyPreset_ = QStringLiteral("custom");
    } else maxUniqueResults_=500;
    // Diagnostic comparisons can retain a historical limit without changing
    // the user's saved profile or conflating retrieval with the result cap.
    bool resultLimitValid=false;
    const int resultLimit=qEnvironmentVariableIntValue("PF_MAX_UNIQUE_RESULTS",&resultLimitValid);
    if (resultLimitValid && resultLimit>=10 && resultLimit<=500) maxUniqueResults_=resultLimit;
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
    settings.analysisMode = analysisMode_.toStdString();
    settings.normalizeSize = normalizeSize_;
    settings.mirrorPoses = mirrorPoses_;
    settings.expandedSearch = expandedSearch_;
    settings.modelPath = modelPath_.toStdString();
    settings.modelChoice = modelChoice_.toStdString();
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

void AnalysisController::setAnalysisMode(const QString& value)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized != QStringLiteral("motion") && normalized != QStringLiteral("static")
        && normalized != QStringLiteral("combined")) return;
    if (analysisMode_ == normalized) return;
    analysisMode_ = normalized;
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

void AnalysisController::setExpandedSearch(bool value)
{
    if (expandedSearch_ == value) return;
    expandedSearch_ = value;
    saveSettings();
    emit settingsChanged();
}

void AnalysisController::setModelPath(const QString& value)
{
    const QString normalized = value.trimmed();
    if (modelPath_ == normalized) return;
    modelPath_ = normalized;
    modelChoice_ = normalized.isEmpty() ? QStringLiteral("yolo26m-pose.onnx")
                                        : QFileInfo(normalized).fileName();
    saveSettings();
    emit settingsChanged();
}

void AnalysisController::selectModel(const QString& filename)
{
    const QString requested = QFileInfo(filename.trimmed()).fileName();
    if (!isSafeModelFilename(requested)) return;
    if (modelDownloading_) {
        modelStatus_ = QStringLiteral("Дождитесь завершения текущей загрузки: ")
            + modelDownloadingName_;
        emit modelStatusChanged();
        return;
    }

    modelChoice_ = requested;
    saveSettings();
    emit settingsChanged();
    if (const auto local = findLocalModelFile(requested)) {
        modelPath_ = QString::fromStdWString(local->wstring());
        modelStatus_ = QStringLiteral("Модель установлена и готова к анализу");
        modelDownloadProgress_ = 1.0;
        saveSettings();
        emit settingsChanged();
        emit modelStatusChanged();
        emit modelDownloadProgressChanged();
        return;
    }

    const auto destination = userModelsRoot() / requested.toStdWString();
    modelDownloading_ = true;
    modelDownloadingName_ = requested;
    modelDownloadProgress_ = 0.0;
    modelStatus_ = QStringLiteral("Скачиваем выбранную модель из GitHub Releases…");
    emit modelStatusChanged();
    emit modelDownloadProgressChanged();
    ++modelCatalogRevision_;
    emit modelCatalogChanged();

    QThread* thread = QThread::create([this, requested, destination] {
        std::string manifestError;
        auto asset = findLocalOrRemoteAsset(requested, manifestError);
        if (!asset.has_value()) {
            const QString message = QStringLiteral("Не удалось получить проверенный manifest для ")
                + requested + (manifestError.empty()
                    ? QString() : QStringLiteral(": ") + QString::fromStdString(manifestError));
            QMetaObject::invokeMethod(this, [this, message] {
                modelDownloading_ = false;
                modelDownloadingName_.clear();
                modelStatus_ = message;
                modelDownloadProgress_ = 0.0;
                emit modelStatusChanged();
                emit modelDownloadProgressChanged();
                ++modelCatalogRevision_;
                emit modelCatalogChanged();
            }, Qt::QueuedConnection);
            return;
        }
        QString compatibilityError;
        if (!modelVersionIsCompatible(*asset, compatibilityError)) {
            QMetaObject::invokeMethod(this, [this, compatibilityError] {
                modelDownloading_ = false;
                modelDownloadingName_.clear();
                modelStatus_ = compatibilityError;
                modelDownloadProgress_ = 0.0;
                emit modelStatusChanged();
                emit modelDownloadProgressChanged();
                ++modelCatalogRevision_;
                emit modelCatalogChanged();
            }, Qt::QueuedConnection);
            return;
        }
        const bool verified = !asset->sha256.empty();
        std::string error;
        const bool ok = pfservices::ModelStore::download(
            *asset,
            destination,
            [this](std::uint64_t received, std::uint64_t total) {
                const double progress = total == 0
                    ? 0.0
                    : std::clamp(static_cast<double>(received) / static_cast<double>(total), 0.0, 1.0);
                QMetaObject::invokeMethod(this, [this, progress] {
                    if (std::abs(modelDownloadProgress_ - progress) < 0.001) return;
                    modelDownloadProgress_ = progress;
                    emit modelDownloadProgressChanged();
                }, Qt::QueuedConnection);
            },
            error);
        const QString message = ok
            ? (verified ? QStringLiteral("Модель скачана, SHA-256 проверен")
                        : QStringLiteral("Модель скачана, размер проверен · SHA-256 отсутствует в manifest"))
            : QStringLiteral("Не удалось скачать модель: ") + QString::fromStdString(error);
        QMetaObject::invokeMethod(this, [this, ok, destination, message] {
            modelDownloading_ = false;
            modelDownloadingName_.clear();
            modelStatus_ = message;
            if (ok) {
                modelPath_ = QString::fromStdWString(destination.wstring());
                modelDownloadProgress_ = 1.0;
                saveSettings();
                emit settingsChanged();
            } else {
                modelDownloadProgress_ = 0.0;
            }
            emit modelStatusChanged();
            emit modelDownloadProgressChanged();
            ++modelCatalogRevision_;
            emit modelCatalogChanged();
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

bool AnalysisController::modelAvailable(const QString& filename) const
{
    const QString requested = QFileInfo(filename.trimmed()).fileName();
    return findLocalModelFile(requested).has_value();
}

QString AnalysisController::modelStatusFor(const QString& filename) const
{
    const QString requested = QFileInfo(filename.trimmed()).fileName();
    if (modelDownloading_ && requested == modelDownloadingName_)
        return QStringLiteral("Скачивается");
    if (modelAvailable(requested)) return QStringLiteral("Установлена");
    return QStringLiteral("Не установлена · скачивается при выборе");
}

void AnalysisController::setCachePath(const QString& value)
{
    const QString normalized = localPathFromInput(value);
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
                                       const QVariantList& selectedIndexes,
                                       bool mergeChronological)
{
    if (exportBusy_) return false;
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
    if (numberingMode == 0) std::stable_sort(selected.begin(), selected.end(), [](const auto& a, const auto& b) {
        return std::tie(a.leftSourceId, a.leftStartSeconds, a.rightStartSeconds)
             < std::tie(b.leftSourceId, b.leftStartSeconds, b.rightStartSeconds);
    });
    const QString normalized = format.trimmed().toUpper();
    if (normalized == QStringLiteral("FFMPEG")) {
        const QString folder = localPathFromInput(outputFolder);
        if (folder.isEmpty()) {
            emit exportFinished(false, QStringLiteral("Выберите папку для MP4-клипов"));
            return false;
        }
        QString requestedPrefix = prefix.trimmed().isEmpty()
            ? QStringLiteral("frame") : prefix.trimmed();
        if (!isSafeExportPrefix(requestedPrefix)) {
            emit exportFinished(false, QStringLiteral(
                "Префикс содержит недопустимые символы: разрешены буквы, цифры, ., _ и -"));
            return false;
        }
        if (!requestedPrefix.endsWith(QLatin1Char('_'))) requestedPrefix += QLatin1Char('_');
        if (!QDir().mkpath(folder)) {
            emit exportFinished(false, QStringLiteral("Не удалось создать папку экспорта: ") + folder);
            return false;
        }
        const pfservices::CutMode mode = cutMode == 1
            ? pfservices::CutMode::Fast : pfservices::CutMode::Exact;
        exportBusy_ = true;
        exportCompleted_ = 0;
        exportClipProgress_ = 0;
        exportTotal_ = static_cast<int>(selected.size() * 2 + (mergeChronological ? 1 : 0));
        emit exportProgressChanged();
        emit exportBusyChanged();
        exportWorker_ = std::jthread([this, selected = std::move(selected), folder, requestedPrefix, mode, mergeChronological,
                                     sceneThreshold = sceneThreshold_](std::stop_token stop) {
        const auto finish = [this](bool success, const QString& message) {
            QMetaObject::invokeMethod(this, [this, success, message] {
                exportBusy_ = false;
                emit exportBusyChanged();
                emit exportFinished(success, message);
            }, Qt::QueuedConnection);
        };
        try {
        std::vector<pfservices::CutRequest> jobs;
        jobs.reserve(selected.size() * 2);
        std::unordered_map<std::string, std::unique_ptr<pfcore::VideoDecoder>> edgeDecoders;
        std::unordered_map<std::string, double> refinedEnds;
        for (std::size_t index = 0; index < selected.size(); ++index) {
            const auto& match = selected[index];
            const QString ordinal = QString::number(static_cast<int>(index + 1)).rightJustified(4, QLatin1Char('0'));
            const auto enqueue = [&](const std::string& source,
                                    double start,
                                    double end,
                                    double sceneEnd,
                                    const QString& side) {
                pfservices::CutRequest request;
                request.inputPath = std::filesystem::path(QString::fromStdString(source).toStdWString());
                request.outputPath = std::filesystem::path((QDir(folder).filePath(
                    requestedPrefix + ordinal + "_" + side + ".mp4")).toStdWString());
                request.startSeconds = std::max(0.0, start);
                request.endSeconds = end;
                if ((mode == pfservices::CutMode::Exact || mergeChronological)
                    && sceneEnd > start && sceneEnd <= end + 0.35) {
                    const auto key = source + "|" + std::to_string(std::llround(sceneEnd * 1000000));
                    const auto [position, inserted] = refinedEnds.try_emplace(key, sceneEnd);
                    if (inserted && !stop.stop_requested()) {
                        try {
                            auto& decoder = edgeDecoders[source];
                            if (!decoder) {
                                decoder = std::make_unique<pfcore::VideoDecoder>();
                                pfcore::VideoDecodeOptions options; options.threads = 2;
                                decoder->open(source, options);
                            }
                            if (const auto edge = pfcore::SceneDetector(sceneThreshold).refineHardCut(*decoder, sceneEnd, stop))
                                position->second = *edge;
                        } catch (...) { /* retain the known edge on an unreadable preview */ }
                    }
                    if (position->second > request.startSeconds)
                        request.endSeconds = std::min(request.endSeconds, position->second);
                }
                request.mode = mode;
                const auto jobIndex = jobs.size();
                const double duration = request.endSeconds - request.startSeconds;
                request.progress = [this, jobIndex, duration, lastPercent = -1](double seconds) mutable {
                    const int percent = duration > 0.0
                        ? static_cast<int>(std::clamp(seconds / duration, 0.0, 1.0) * 100.0) : 0;
                    if (percent == lastPercent) return;
                    lastPercent = percent;
                    QMetaObject::invokeMethod(this, [this, jobIndex, percent] {
                        exportCompleted_ = static_cast<int>(jobIndex);
                        exportClipProgress_ = percent;
                        emit exportProgressChanged();
                    }, Qt::QueuedConnection);
                };
                jobs.push_back(std::move(request));
            };
            const auto leftClip = pfcore::parallelClipRange(match.leftStartSeconds, match.leftEndSeconds,
                match.leftSceneStartSeconds, match.leftSceneEndSeconds);
            const auto rightClip = pfcore::parallelClipRange(match.rightStartSeconds, match.rightEndSeconds,
                match.rightSceneStartSeconds, match.rightSceneEndSeconds);
            enqueue(match.leftSourceId, leftClip.start, leftClip.end, match.leftSceneEndSeconds, QStringLiteral("A"));
            enqueue(match.rightSourceId, rightClip.start, rightClip.end, match.rightSceneEndSeconds, QStringLiteral("B"));
        }
        if (mergeChronological) {
            const auto output = std::filesystem::path(QDir(folder).filePath(requestedPrefix + "combined.mp4").toStdWString());
            const auto result = pfservices::exportChronologicalMontage(std::move(jobs), output, stop,
                [this](std::size_t done, std::size_t total, int percent) {
                    QMetaObject::invokeMethod(this, [this, done, total, percent] {
                        exportCompleted_ = static_cast<int>(done);
                        exportTotal_ = static_cast<int>(total);
                        exportClipProgress_ = percent;
                        emit exportProgressChanged();
                    }, Qt::QueuedConnection);
                });
            finish(result.success, result.success ? QStringLiteral("MP4: ") + QString::fromStdWString(output.wstring())
                : QString::fromStdString(result.error));
            return;
        }
        const auto batch = pfservices::runExportQueue(jobs, stop, [this](std::size_t done, std::size_t) {
            QMetaObject::invokeMethod(this, [this, done] {
                exportCompleted_ = static_cast<int>(done);
                exportClipProgress_ = 0;
                emit exportProgressChanged();
            }, Qt::QueuedConnection);
        });
        int written = 0;
        QStringList failures;
        for (std::size_t i = 0; i < batch.completed.size(); ++i) {
            const auto& item = batch.completed[i];
            if (item.success) ++written;
            else if (!item.cancelled) failures << QString::fromStdWString(jobs[i].outputPath.filename().wstring())
                + QStringLiteral(": ") + QString::fromStdString(item.error);
        }
        const QString message = QStringLiteral("FFmpeg: %1 / %2 MP4; errors: %3%4")
            .arg(written).arg(jobs.size()).arg(failures.size())
            .arg(batch.cancelled ? QStringLiteral("; cancelled") : QString());
        // Keep individual failures, not just the last failed encoder/file.
        finish(!batch.cancelled && failures.isEmpty(), message + (failures.isEmpty()
            ? QString() : QStringLiteral("\n") + failures.join(QLatin1Char('\n'))));
        } catch (const std::exception& error) {
            finish(false, QStringLiteral("FFmpeg: ") + QString::fromUtf8(error.what()));
        }
        });
        return true;
    }
    pfexporters::ExportOptions options;
    if (normalized == QStringLiteral("CSV")) options.format = pfexporters::ExportFormat::Csv;
    else if (normalized == QStringLiteral("TXT")) options.format = pfexporters::ExportFormat::Txt;
    else if (normalized == QStringLiteral("EDL")) options.format = pfexporters::ExportFormat::Edl;
    else if (normalized == QStringLiteral("FCPXML")) options.format = pfexporters::ExportFormat::FcpXml;
    else if (normalized == QStringLiteral("AEP")) options.format = pfexporters::ExportFormat::Aep;
    else options.format = pfexporters::ExportFormat::Json;
    options.numbering = numberingMode == 1 ? pfexporters::NumberingMode::RenumberSorted
                                           : pfexporters::NumberingMode::AsInVideo;
    options.cutMode = cutMode == 1 ? pfexporters::CutMode::Fast : pfexporters::CutMode::Exact;
    const QString exportPrefix = prefix.trimmed().isEmpty() ? QStringLiteral("frame_") : prefix.trimmed();
    if (!isSafeExportPrefix(exportPrefix)) {
        emit exportFinished(false, QStringLiteral(
            "Префикс содержит недопустимые символы: разрешены буквы, цифры, ., _ и -"));
        return false;
    }
    options.outputFolder = localPathFromInput(outputFolder).toStdString();
    options.filePrefix = exportPrefix.toStdString();
    options.framesPerSecond = sourceFps_;
    std::string error;
    for (auto& match : selected) {
        const auto left = pfcore::parallelClipRange(match.leftStartSeconds, match.leftEndSeconds,
            match.leftSceneStartSeconds, match.leftSceneEndSeconds);
        const auto right = pfcore::parallelClipRange(match.rightStartSeconds, match.rightEndSeconds,
            match.rightSceneStartSeconds, match.rightSceneEndSeconds);
        match.leftStartSeconds = left.start; match.leftEndSeconds = left.end;
        match.rightStartSeconds = right.start; match.rightEndSeconds = right.end;
    }
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
    QSaveFile file(localPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    const QByteArray data = QJsonDocument(object).toJson(QJsonDocument::Indented);
    if (file.write(data) != data.size()) return false;
    return file.commit();
}

QVariantMap AnalysisController::importTheme(const QString& path) const
{
    QString localPath = path;
    if (localPath.startsWith(QStringLiteral("file:"))) localPath = QUrl(localPath).toLocalFile();
    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly)) return {};
    if (file.size() < 0 || file.size() > 4 * 1024 * 1024) return {};
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) return {};
    const QJsonObject object = document.object();
    if (object.value(QStringLiteral("schema")).toString() != QStringLiteral("parallel-finder-theme")) return {};
    return object.toVariantMap();
}

QString AnalysisController::videoSourceUrl(const QString& sourcePath) const
{
    const QString path = localPathFromInput(sourcePath);
    return path.isEmpty() ? QString{} : QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
}

void AnalysisController::setStatus(const QString& status)
{
    if (status_ == status) return;
    status_ = status;
    emit statusChanged();
}

bool AnalysisController::resumePendingInspection()
{
    if (!pendingInspectionPaths_) return false;
    const QStringList paths = std::move(*pendingInspectionPaths_);
    pendingInspectionPaths_.reset();
    inspectFiles(paths);
    return true;
}

QVariantMap AnalysisController::summaryForSource(const QString& path) const
{
    return sourceSummaries_.value(localPathFromInput(path)).toMap();
}

void AnalysisController::inspectFiles(const QStringList& paths)
{
    const QStringList normalized = normalizedPaths(paths);
    deferredAnalyzePaths_.clear();
    results_.clear();
    matches_.clear();
    fileCount_ = 0;
    sourceSummaries_.clear();
    frameCount_ = 0;
    durationSeconds_ = 0.0;
    sceneCount_ = 0;
    poseDetectionCount_ = 0;
    matchCount_ = 0;
    sourceFps_ = 0.0;
    analysisCompleted_ = false;
    emit analysisStateChanged();
    emit resultsChanged();
    emit summaryChanged();
    setProgress(0.0, normalized.isEmpty() ? QStringLiteral("Нет файлов")
                                          : QStringLiteral("Открываем файлы"), 0, 0);
    if (busy_) {
        pendingInspectionPaths_ = normalized;
        if (analysisCancel_) analysisCancel_->store(true, std::memory_order_relaxed);
        return;
    }
    if (normalized.isEmpty()) {
        setStatus(QStringLiteral("Добавьте видео для анализа"));
        return;
    }
    busy_ = true;
    emit busyChanged();
    setStatus(QStringLiteral("Открываем видео…"));
    QThread* thread = QThread::create([this, paths = normalized] {
        int files = 0;
        qlonglong frames = 0;
        double duration = 0.0;
        QString error;
        QVariantMap summaries;
        for (const QString& path : paths) {
            try {
                pfcore::VideoDecoder decoder;
                decoder.open(path.toStdString());
                const auto& info = decoder.info();
                ++files;
                duration += info.durationSeconds;
                if (info.frameRate > 0.0) frames += static_cast<qlonglong>(info.durationSeconds * info.frameRate);
                summaries.insert(path, QVariantMap{{"fileCount", 1},
                    {"frameCount", static_cast<qlonglong>(std::max(0.0, info.durationSeconds * info.frameRate))},
                    {"durationSeconds", info.durationSeconds}, {"sceneCount", 0}, {"matchCount", 0}});
            } catch (const std::exception& exception) {
                error = QString::fromUtf8(exception.what());
                summaries.insert(path, QVariantMap{{"fileCount", 0}, {"error", error}});
            }
        }
        QMetaObject::invokeMethod(this, [this, files, frames, duration, error, summaries] {
            if (pendingInspectionPaths_) {
                busy_ = false;
                emit busyChanged();
                resumePendingInspection();
                return;
            }
            fileCount_ = files;
            frameCount_ = frames;
            durationSeconds_ = duration;
            sourceSummaries_ = summaries;
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
    QStringList normalized = normalizedPaths(paths);
    // Candidate tie breaks and overlap aliases use window indices. Keep the
    // analysis order stable when the same files are selected in another order.
    normalized.sort(Qt::CaseSensitive);
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
    if (modelDownloading_) {
        setProgress(0.0, QStringLiteral("Скачиваем модель"), 0, 0);
        setStatus(QStringLiteral("Дождитесь завершения загрузки модели ") + modelDownloadingName_);
        return;
    }
    if (!modelAvailable(modelChoice_)) {
        setProgress(0.0, QStringLiteral("Модель не установлена"), 0, 0);
        setStatus(QStringLiteral("Модель ") + modelChoice_
                  + QStringLiteral(" ещё не установлена. Выберите её в каталоге моделей."));
        return;
    }
    // A manually selected execution provider is a hard requirement. Do not
    // silently fall back to CPU after the user explicitly chose CUDA, TensorRT
    // or DirectML; report the real probe reason before touching the files.
    if (AppInfo::instance()->backendInitializing()) {
        setStatus(QStringLiteral("Проверяем провайдеры — дождитесь готовности"));
        return;
    }
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
    fileCount_ = 0;
    frameCount_ = 0;
    durationSeconds_ = 0;
    sceneCount_ = 0;
    sourceSummaries_.clear();
    emit summaryChanged();
    busy_ = true;
    emit busyChanged();
    setProgress(0.0, expandedSearch_ ? QStringLiteral("Подготавливаем анализ с нуля")
                                  : QStringLiteral("Подготавливаем анализ"), 0, 0);
    setStatus(expandedSearch_ ? QStringLiteral("Повторный анализ: сбрасываем кэш выбранных видео…")
                             : QStringLiteral("Декодируем кадры и ищем смены сцен…"));
    const auto cancel = std::make_shared<std::atomic_bool>(false);
    analysisCancel_ = cancel;
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
    const QString analysisMode = analysisMode_;
    const bool expandedSearch = expandedSearch_;
    const bool normalizeSize = normalizeSize_;
    const bool mirrorPoses = mirrorPoses_;
    const QString previewToken = QString::number(QDateTime::currentMSecsSinceEpoch());
    QThread* thread = QThread::create([this, cancel, paths = normalized, similarityThreshold, candidateThreshold, repeatGap,
                                        sameFileGap, crossFileGap, duplicateWindow, noiseFactor,
                                        maxUniqueResults, timeWeight, providerChoice,
                                        qualityProfile, analysisMode, expandedSearch, normalizeSize, mirrorPoses, previewToken] {
        try {
        int files = 0;
        int scenes = 0;
        int poseDetections = 0;
        int matches = 0;
        QVariantList resultRecords;
        QVariantMap summaries;
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
        const auto isCancelled = [&cancel] {
            return cancel && cancel->load(std::memory_order_relaxed);
        };
        const auto markCancelled = [&error, &isCancelled] {
            if (error.isEmpty() && isCancelled()) error = QStringLiteral("Остановлено пользователем");
        };
        const pfservices::Settings settings = pfservices::SettingsStore().load(settingsError);
        (void)settingsError;
        auto model = findPoseModel();
        if (model.empty()) {
            QMetaObject::invokeMethod(this, [this, cancel] {
                busy_ = false;
                emit busyChanged();
                if (analysisCancel_ == cancel) analysisCancel_.reset();
                if (resumePendingInspection()) return;
                setProgress(0.0, QStringLiteral("Модель не найдена"), 0, 0);
                setStatus(QStringLiteral("Модель поз не найдена. Укажите её в settings.json или в папке models."));
            }, Qt::QueuedConnection);
            return;
        }
        // Fixed-batch exports are intended for GPU execution. ONNX Runtime's
        // CPU EP still computes every padded slot, so running a b8/b16 model
        // one frame at a time is slower than the matching b1 export. When the
        // user explicitly runs CPU (or Auto resolves to CPU), prefer the b1
        // sibling if it is installed; this keeps the UI choice usable instead
        // of turning a four-minute clip into an apparent hang.
        const auto requestedProvider = pfgpu::parseProvider(providerChoice.toStdString());
        const bool cpuOnly = providerChoice == QStringLiteral("cpu")
            || (providerChoice == QStringLiteral("auto")
                && requestedProvider.has_value()
                && pfgpu::resolveProvider(*requestedProvider) == pfgpu::Provider::Cpu);
        const std::string modelFilename = model.filename().string();
        if (cpuOnly && (modelFilename.find("-b8") != std::string::npos
                        || modelFilename.find("-b16") != std::string::npos)) {
            auto cpuModel = model;
            const auto marker = cpuModel.filename().string().find("-b8") != std::string::npos
                ? "-b8" : "-b16";
            auto name = cpuModel.filename().string();
            name.replace(name.find(marker), std::char_traits<char>::length(marker), "-b1");
            cpuModel.replace_filename(name);
            std::error_code modelError;
            if (std::filesystem::is_regular_file(cpuModel, modelError)) model = cpuModel;
        }
        QVariantMap inspectedSummaries;
        double inspectedDuration = 0;
        qlonglong inspectedFrames = 0;
        for (const QString& path : paths) {
            if (isCancelled()) { markCancelled(); break; }
            try {
                pfcore::VideoDecoder probe;
                probe.open(path.toStdString());
                const auto& info = probe.info();
                totalFramesEstimate += static_cast<qlonglong>(std::max(1.0, info.durationSeconds * info.frameRate));
                const auto frameEstimate = static_cast<qlonglong>(std::max(0.0, info.durationSeconds * info.frameRate));
                inspectedDuration += info.durationSeconds;
                inspectedFrames += frameEstimate;
                inspectedSummaries.insert(path, QVariantMap{{"fileCount", 1}, {"frameCount", frameEstimate},
                    {"durationSeconds", info.durationSeconds}, {"sceneCount", 0}, {"matchCount", 0}});
            } catch (...) {
            }
        }
        QMetaObject::invokeMethod(this, [this, cancel, totalFramesEstimate, inspectedSummaries, inspectedDuration, inspectedFrames] {
            if (pendingInspectionPaths_ || cancel->load(std::memory_order_relaxed)) return;
            fileCount_ = inspectedSummaries.size();
            durationSeconds_ = inspectedDuration;
            frameCount_ = inspectedFrames;
            sourceSummaries_ = inspectedSummaries;
            emit summaryChanged();
            setProgress(0.0, QStringLiteral("Читаем кадры"), 0, totalFramesEstimate);
        }, Qt::QueuedConnection);
        std::unique_ptr<pfservices::PfCache> analysisCache;
        try {
            std::filesystem::path cacheRoot = settings.cachePath.empty()
                ? std::filesystem::u8path(pfservices::SettingsStore::defaultDirectory()) / "cache"
                : std::filesystem::u8path(settings.cachePath);
            // A smoke benchmark can use a fresh, isolated cache without
            // deleting the user's data or changing their saved preferences.
            const QString benchmarkCache = qEnvironmentVariable("PF_BENCHMARK_CACHE_ROOT");
            if (QCoreApplication::arguments().contains(QStringLiteral("--pf-analysis-smoke"))
                && !benchmarkCache.isEmpty())
                cacheRoot = std::filesystem::u8path(benchmarkCache.toStdString());
            analysisCache = std::make_unique<pfservices::PfCache>(cacheRoot, settings.cacheLimitBytes);
        } catch (const std::exception&) {
            // Cache is an optimization; analysis remains usable when its path
            // is unavailable or malformed.
        }
        std::shared_ptr<pfgpu::PoseEstimator> pose =
            sharedPoseEstimator(model, providerChoice, settings.processingThreads);
        const auto reidModel = findBodyReIdModel();
        const auto faceDetectorPath = findLocalModelFile(QStringLiteral("face_detection_yunet_2023mar.onnx"));
        const auto faceRecognizerPath = findLocalModelFile(QStringLiteral("face_recognition_sface_2021dec.onnx"));
        std::shared_ptr<pfgpu::FaceEstimator> face;
        if (faceDetectorPath && faceRecognizerPath)
            face = sharedFaceEstimator(*faceDetectorPath, *faceRecognizerPath, providerChoice);
        QString faceFailure;
        const bool reidModelPresent = !reidModel.empty();
        bool reidReady = false;
        QString reidFailure;
        std::shared_ptr<pfgpu::ReIdEstimator> reid;
        if (reidModelPresent) {
            try {
                reid = sharedReIdEstimator(reidModel, providerChoice, settings.processingThreads);
                reidReady = true;
            } catch (const std::exception& exception) {
                reidFailure = QString::fromUtf8(exception.what());
            }
        }
        QString sharedIdentityStatus;
        std::unordered_map<std::string,std::string> sequenceCacheKeys;
        std::unordered_map<std::string,std::string> sourceGenerations;
        QJsonObject frozenWindowKeys;
        if (QCoreApplication::arguments().contains(QStringLiteral("--pf-analysis-smoke"))) {
            QFile file(qEnvironmentVariable("PF_AUDIT_FROZEN_WINDOW_KEYS"));
            if (!file.fileName().isEmpty() && file.open(QIODevice::ReadOnly))
                frozenWindowKeys=QJsonDocument::fromJson(file.readAll()).object();
        }
        std::vector<pfcore::MotionWindow> windows;
        for (const QString& path : paths) {
            if (isCancelled()) { markCancelled(); break; }
            try {
                if (analysisCache) {
                    if (expandedSearch) {
                        std::string cacheError;
                        // Migrate identifiable legacy face entries before the
                        // primary observations that describe their owner go.
                        for(const auto& oldKey:analysisCache->keysForSource(path.toStdString())) {
                            if(!oldKey.starts_with("motion-"))continue;
                            const auto bytes=analysisCache->get(oldKey);if(!bytes)continue;
                            std::vector<pfcore::MotionWindow> oldWindows;int oldScenes=-1;
                            if(!deserializeMotionWindows(*bytes,oldWindows,oldScenes))continue;
                            const auto faceKey=matchedFaceCacheKey(oldKey,sourceFaceCentroid(oldWindows,path.toStdString()));
                            if(analysisCache->get(faceKey) && !analysisCache->rememberSourceKey(path.toStdString(),faceKey,cacheError))
                                throw std::runtime_error("Cannot identify selected video cache: "+cacheError);
                        }
                        if (!analysisCache->resetSource(path.toStdString(), cacheError))
                            throw std::runtime_error("Cannot reset selected video cache: " + cacheError);
                    }
                    sourceGenerations[path.toStdString()] = analysisCache->sourceGeneration(path.toStdString());
                }
                pfcore::VideoDecoder decoder;
                pfcore::VideoDecodeOptions decodeOptions;
                decodeOptions.threads = static_cast<int>(std::min<std::size_t>(settings.processingThreads, 8));
                decodeOptions.maxWidth = 1280;
                decodeOptions.maxHeight = 720;
                // Avoid CUDA initialization/download overhead on ordinary HD.
                // 4K retains accelerated decode with the validated CPU resize.
                decodeOptions.minimumNvidiaPixels = 1920ULL * 1080ULL + 1;
                decodeOptions.preferNvidia = providerChoice == QStringLiteral("cuda")
                    || providerChoice == QStringLiteral("tensorrt")
                    || (providerChoice == QStringLiteral("auto") && !cpuOnly
                        && requestedProvider.has_value()
                        && (pfgpu::resolveProvider(*requestedProvider) == pfgpu::Provider::Cuda
                            || pfgpu::resolveProvider(*requestedProvider) == pfgpu::Provider::TensorRt));
                const QString decodeOverride = qEnvironmentVariable("PF_VIDEO_DECODE").toLower();
                if (decodeOverride == QStringLiteral("cpu")) decodeOptions.preferNvidia = false;
                else if (decodeOverride == QStringLiteral("nvdec")) {
                    decodeOptions.preferNvidia = true;
                    decodeOptions.minimumNvidiaPixels = 0; // explicit benchmark override
                }
                decoder.open(path.toStdString(), decodeOptions);
                // Decode only the working resolution needed by pose/ReID.
                // The separate preview decoder below remains full-resolution
                // when the user opens an A/B result.
                decoder.setRgbaMaxDimensions(1280, 720);
                const auto info = decoder.info();
                std::vector<pfcore::MotionWindow> cachedWindows;
                bool cacheHit = false;
                int cachedSceneCount = -1;
                // Bump whenever the detector/window contract changes. Reusing
                // a pre-ReID or pre-batched cache can silently produce empty
                // track windows and make a valid source look matchless.
                // The cache contains derived identity and motion windows.
                // Bump this contract whenever association or the identity
                // policy changes; otherwise a stricter matcher can still
                // display candidates produced by an older pipeline.
                std::string cacheKey = "motion-v43|rc16-independent-admission|rc16-lead-recovery|duplicate-identity-veto|shot-local-tracking|duration-first-lead|co-visible-identities|strict-source-lead|sparse-face-veto|stable-motion-cuts|bidirectional-low-contrast-cuts|spatial-fades|supported-static-runs|matcher-mirror|scene4fps|infer=1280x720|decode="
                    + decoder.diagnostics().backend
                    + "|pose=" + cacheFileFingerprint(model) + "|"
                    + providerChoice.toStdString() + "|reid="
                    + (reidModelPresent ? cacheFileFingerprint(reidModel) : std::string("none")) + "|"
                    + "face=" + (faceDetectorPath && faceRecognizerPath
                        ? cacheFileFingerprint(*faceDetectorPath) + cacheFileFingerprint(*faceRecognizerPath)
                        : std::string("none")) + "|"
                    + "quality=" + qualityProfile.toStdString() + "|"
                    + "mode=" + analysisMode.toStdString() + "|scene="
                    + std::to_string(settings.sceneThreshold) + ":"
                    + std::to_string(settings.sceneMinFrames) + ":"
                    + std::to_string(settings.sceneAdaptiveMultiplier) + "|source="
                    + cacheFileFingerprint(path.toStdString());
                if (requestedProvider) {
                    const auto resolved = pfgpu::resolveProvider(*requestedProvider);
                    // Auto may resolve differently after installing a runtime;
                    // cached observations from the former backend are not a
                    // substitute for analyzing with the newly selected one.
                    if (*requestedProvider == pfgpu::Provider::Auto)
                        cacheKey += std::string("|resolved=") + pfgpu::providerName(resolved);
                    // V2 now really applies TensorRT's existing FP16 policy.
                    // Do not reuse legacy FP32 observations as FP16 results.
                    // CUDA's bit-exact preparation keeps its current cache.
                    if (resolved == pfgpu::Provider::TensorRt)
                        cacheKey += "|trt-v2-fp16-v1|ort=" + pfgpu::ortRuntimeVersion();
                }
                if (const auto frozen=frozenWindowKeys.value(path).toString();!expandedSearch && !frozen.isEmpty()) {
                    const auto original=frozen.toStdString();
                    const auto suffix=cacheFileFingerprint(path.toStdString());
                    if (!original.starts_with("motion-v43|") || !original.ends_with(suffix))
                        throw std::runtime_error("Frozen observations do not match the current source fingerprint");
                    cacheKey=original;
                    if (qEnvironmentVariableIsSet("PF_DEBUG_ANALYSIS"))
                        std::fprintf(stderr,"PF_DEBUG_FROZEN_OBSERVATIONS source=%s legacy_window_replay=1 new_model_inference=0\n",path.toStdString().c_str());
                }
                const auto generation = sourceGenerations[path.toStdString()];
                if (!generation.empty()) cacheKey += "|generation=" + generation;
                sequenceCacheKeys[path.toStdString()]=cacheKey;
                if (analysisCache && !expandedSearch) {
                    if (const auto cached = analysisCache->get(cacheKey))
                        cacheHit = deserializeMotionWindows(*cached, cachedWindows, cachedSceneCount);
                }
                ++files;
                duration += info.durationSeconds;
                if (sourceFps <= 0.0) sourceFps = info.frameRate;
                frames += static_cast<qlonglong>(std::max(0.0, info.durationSeconds * info.frameRate));
                summaries.insert(path, QVariantMap{{"fileCount", 1},
                    {"frameCount", static_cast<qlonglong>(std::max(0.0, info.durationSeconds * info.frameRate))},
                    {"durationSeconds", info.durationSeconds}, {"sceneCount", 0}, {"matchCount", 0}});
                if (cacheHit && cachedSceneCount >= 0) {
                    // Upgrade only spatial view evidence from old pose caches.
                    // The expensive detector/identity observations stay intact.
                    const bool missingViews = std::any_of(cachedWindows.begin(), cachedWindows.end(), [](const auto& window) {
                        return window.staticFrameSet && !window.sceneViewSampled;
                    });
                    const auto viewFrames = ensureSceneViews(cachedWindows, decoder, isCancelled);
                    if (isCancelled()) { markCancelled(); break; }
                    if (analysisCache && missingViews) {
                        std::string cacheError;
                        analysisCache->putForSource(path.toStdString(),cacheKey, serializeMotionWindows(cachedWindows, cachedSceneCount), cacheError);
                    }
                    scenes += cachedSceneCount;
                    auto summary = summaries.value(path).toMap();
                    summary.insert("sceneCount", cachedSceneCount);
                    summaries.insert(path, summary);
                    for (auto& cached : cachedWindows) {
                        windows.push_back(std::move(cached));
                        previewA.push_back(QString());
                        previewB.push_back(QString());
                    }
                    if (qEnvironmentVariableIsSet("PF_DEBUG_ANALYSIS"))
                        std::fprintf(stderr, "PF_DEBUG_TIMING complete_cache_hit=%d window_cache_hit=1 decoded=%llu view_samples=%zu pose_samples=0 source=%s\n",
                            viewFrames == 0 ? 1 : 0, static_cast<unsigned long long>(decoder.diagnostics().decodedFrames), viewFrames, qPrintable(path));
                    continue;
                }
                std::vector<std::vector<std::uint8_t>> sceneBuffers;
                const bool profilePipeline = qEnvironmentVariableIsSet("PF_DEBUG_ANALYSIS");
                QElapsedTimer pipelineTimer;
                pipelineTimer.start();
                const auto stageNow = [&] { return profilePipeline ? pipelineTimer.nsecsElapsed() : 0LL; };
                qint64 poseNs = 0, faceNs = 0, reidNs = 0, trackerNs = 0, inferenceSampleNs = 0, sceneThumbnailNs = 0;
                std::vector<double> sceneTimestamps;
                sceneBuffers.reserve(static_cast<std::size_t>(std::max(1.0, info.durationSeconds * 4.0)) + 1U);
                sceneTimestamps.reserve(sceneBuffers.capacity());
                pfcore::PersonTracker tracker;
                pfcore::PersonTracker rc16Tracker(.30, 1.0, false);
                pfcore::DecodedFrame firstFrame;
                pfcore::DecodedFrame lastFrame;
                pfcore::DecodedFrame frame;
                std::size_t poseSampleIndex = 0;
                double previousPoseTimestamp = -1.0;
                // Keep motion sampling stable in time rather than in frame
                // numbers. This behaves correctly for 24/30/60 fps and VFR
                // input while still decoding RGBA only for selected frames.
                const double targetPoseFps = qualityProfile == QStringLiteral("fast") ? 6.0
                    : qualityProfile == QStringLiteral("medium") ? 10.0 : 15.0;
                // Four thumbnails per second preserve short edited shots.
                // Sampling is timestamp-based so VFR input does not drift;
                // readNext can stay metadata-only and convert only the exact
                // decoded frame selected for scene analysis.
                // Appearance changes much more slowly than pose. Sampling it
                // by elapsed time, rather than every second pose sample,
                // prevents "maximum" mode from spending most of its runtime
                // re-identifying nearly identical frames. The 0.5 s floor
                // still gives several independent crops per motion window.
                const double reidIntervalSeconds = 0.50;
                double nextReIdTimestamp = -std::numeric_limits<double>::infinity();
                struct PendingPoseSample {
                    double timestamp = 0.0;
                    int width = 0;
                    int height = 0;
                    std::size_t sampleIndex = 0;
                    std::vector<std::uint8_t> rgba;
                    const std::uint8_t* borrowed = nullptr;
                    const std::uint8_t* pixels() const noexcept { return borrowed ? borrowed : rgba.data(); }
                };
                std::vector<PendingPoseSample> pendingPose;
                const std::size_t requestedPoseBatch = pose
                    ? std::max<std::size_t>(1, pose->params().batchSize) : 1U;
                // A fixed-batch model must keep its source frames alive until
                // inference returns.  Bound that staging buffer so a 4K clip
                // cannot turn a b8/b16 model into a multi-hundred-megabyte
                // allocation before ONNX Runtime even starts.
                constexpr std::uint64_t kPoseBatchMemoryBudget = 128ULL * 1024ULL * 1024ULL;
                // Pending samples are capped at 1280x720 by
                // makeInferenceSample(), so the batch budget reflects the
                // actual tensor staging size rather than the source's 4K
                // dimensions.
                const std::uint64_t inferenceWidth = std::min<std::uint64_t>(
                    1280ULL, static_cast<std::uint64_t>(std::max(1, info.width)));
                const std::uint64_t inferenceHeight = std::min<std::uint64_t>(
                    720ULL, static_cast<std::uint64_t>(std::max(1, info.height)));
                const std::uint64_t frameBytes = std::max<std::uint64_t>(1,
                    inferenceWidth * inferenceHeight * 4ULL);
                const std::size_t memoryBoundBatch = static_cast<std::size_t>(std::max<std::uint64_t>(
                    1, kPoseBatchMemoryBudget / frameBytes));
                const std::size_t poseBatchSize = std::min(requestedPoseBatch, memoryBoundBatch);
                const auto processPose = [&](const PendingPoseSample& sample,
                                              const std::vector<pfgpu::PoseDetection>& detections) {
                    poseDetections += static_cast<int>(detections.size());
                    std::vector<pfcore::PersonDetection> frameDetections;
                    frameDetections.reserve(detections.size());
                    const bool sampleAppearance = (reid || face)
                        && sample.timestamp + 1e-9 >= nextReIdTimestamp;
                    if (sampleAppearance)
                        nextReIdTimestamp = sample.timestamp + reidIntervalSeconds;
                    std::vector<pfgpu::ReIdImage> reidImages;
                    std::vector<std::size_t> reidDetectionIndices;
                    if (sampleAppearance) reidImages.reserve(detections.size());
                    if (sampleAppearance) reidDetectionIndices.reserve(detections.size());
                    for (const auto& detection : detections) {
                        pfcore::PersonDetection person;
                        person.timestampSeconds = sample.timestamp;
                        person.box = {detection.left, detection.top, detection.right, detection.bottom};
                        person.confidence = detection.confidence;
                        for (std::size_t i = 0; i + 2 < detection.keypoints.size(); i += 3) {
                            person.keypoints.push_back({detection.keypoints[i], detection.keypoints[i + 1], detection.keypoints[i + 2]});
                            person.keypointConfidence += detection.keypoints[i + 2];
                        }
                        if (!person.keypoints.empty())
                            person.keypointConfidence /= static_cast<double>(person.keypoints.size());
                        frameDetections.push_back(std::move(person));
                        if (sampleAppearance && face && detection.confidence >= 0.4F) {
                            try {
                                const auto stageStart = stageNow();
                                frameDetections.back().faceEmbedding = face->infer({sample.width, sample.height,
                                    sample.pixels(), detection.left, detection.top, detection.right, detection.bottom});
                                faceNs += stageNow() - stageStart;
                            } catch (const std::exception& exception) {
                                faceFailure = QString::fromUtf8(exception.what());
                                face.reset();
                            }
                        }
                        // A tiny or low-confidence person crop is mostly
                        // background. Feeding it into ReID is both slow and a
                        // frequent source of false identity similarity.
                        const float cropWidth = std::max(0.0F, detection.right - detection.left);
                        const float cropHeight = std::max(0.0F, detection.bottom - detection.top);
                        const bool viableReIdCrop = sampleAppearance
                            && detection.confidence >= 0.40F
                            && cropWidth >= std::max(24.0F, sample.width * 0.035F)
                            && cropHeight >= std::max(48.0F, sample.height * 0.08F)
                            // Edited close-ups are wide upper-body crops. The
                            // old full-body aspect gate silently excluded them.
                            && cropHeight / std::max(cropWidth, 1.0F) >= 0.35F
                            && cropHeight / std::max(cropWidth, 1.0F) <= 5.0F;
                        if (viableReIdCrop) {
                            reidImages.push_back({sample.width, sample.height, sample.pixels(),
                                                  detection.left, detection.top,
                                                  detection.right, detection.bottom});
                            reidDetectionIndices.push_back(frameDetections.size() - 1U);
                        }
                    }
                    if (sampleAppearance && reid && !reidImages.empty()) {
                        try {
                            const auto stageStart = stageNow();
                            const auto embeddings = reid->inferBatch(reidImages);
                            reidNs += stageNow() - stageStart;
                            for (std::size_t image = 0;
                                 image < std::min(embeddings.size(), reidDetectionIndices.size());
                                 ++image) {
                                const std::size_t detection = reidDetectionIndices[image];
                                frameDetections[detection].appearanceEmbedding = embeddings[image];
                                frameDetections[detection].appearanceConfidence = embeddings[image].empty() ? 0.0 : 1.0;
                            }
                        } catch (const std::exception& exception) {
                            reidFailure = QString::fromUtf8(exception.what());
                            reid.reset();
                            reidReady = false;
                        }
                    }
                    const double frameDuration = previousPoseTimestamp >= 0.0
                        ? std::max(0.0, sample.timestamp - previousPoseTimestamp)
                        : (info.frameRate > 0.0 ? 1.0 / info.frameRate : 0.0);
                    const auto trackerStart = stageNow();
                    tracker.update(sample.timestamp, frameDuration, frameDetections);
                    rc16Tracker.update(sample.timestamp, frameDuration, frameDetections);
                    trackerNs += stageNow() - trackerStart;
                    previousPoseTimestamp = sample.timestamp;
                };
                const auto flushPoseBatch = [&] {
                    if (pendingPose.empty() || !pose) return;
                    std::vector<pfgpu::PoseImage> images;
                    images.reserve(pendingPose.size());
                    for (const auto& sample : pendingPose)
                        images.push_back({sample.width, sample.height, sample.pixels()});
                    const auto stageStart = stageNow();
                    const auto batchDetections = pose->inferBatch(images);
                    poseNs += stageNow() - stageStart;
                    for (std::size_t index = 0;
                         index < std::min(batchDetections.size(), pendingPose.size()); ++index)
                        processPose(pendingPose[index], batchDetections[index]);
                    pendingPose.clear();
                };
                // CPU inference can already occupy every core. Do not add
                // decode contention there without a measured benefit; the
                // bounded overlap is enabled by default for GPU inference.
                const QString prefetchOverride = qEnvironmentVariable("PF_ANALYSIS_PREFETCH");
                const bool prefetchFrames = prefetchOverride == QStringLiteral("1")
                    || (prefetchOverride != QStringLiteral("0") && !cpuOnly);
                pfcore::VideoSampleReader sampleReader(decoder,
                    pose && !cacheHit ? targetPoseFps : 0.0, 4.0, isCancelled,
                    prefetchFrames, 3, profilePipeline);
                const auto framesBeforeFile = processedFrames;
                qlonglong lastProgressFrames = processedFrames;
                pfcore::VideoSample decodedSample;
                for (;;) {
                    // Decode timestamps first. Once a timestamp is selected,
                    // convert the retained AVFrame exactly once for scene and/or
                    // pose inference. This avoids frame-index drift on VFR input.
                    if (!sampleReader.readNext(decodedSample)) break;
                    if (isCancelled()) { markCancelled(); break; }
                    frame = std::move(decodedSample.frame);
                    const bool samplePose = decodedSample.pose;
                    const bool sampleScene = decodedSample.scene;
                    if (samplePose) ++poseSampleIndex;
                    processedFrames = framesBeforeFile + static_cast<qlonglong>(decodedSample.decodedFrames);
                    if (processedFrames - lastProgressFrames >= 10 || processedFrames == totalFramesEstimate) {
                        lastProgressFrames = processedFrames;
                        const double localProgress = totalFramesEstimate > 0
                            ? static_cast<double>(processedFrames) / static_cast<double>(totalFramesEstimate)
                            : 0.0;
                        QMetaObject::invokeMethod(this, [this, localProgress, processedFrames, totalFramesEstimate] {
                            setProgress(localProgress * 0.85, QStringLiteral("Анализируем движение"), processedFrames, totalFramesEstimate);
                        }, Qt::QueuedConnection);
                    }
                    if (firstFrame.rgba.empty() && !frame.rgba.empty()) firstFrame = frame;
                    if (sampleScene && !frame.rgba.empty()) {
                        const auto before = stageNow();
                        sceneBuffers.push_back(sceneThumbnail(frame));
                        sceneTimestamps.push_back(frame.timestampSeconds);
                        sceneThumbnailNs += stageNow() - before;
                    }
                    if (samplePose && !frame.rgba.empty()) {
                        const auto before = stageNow();
                        InferenceSample inference = makeInferenceSample(frame, poseBatchSize <= 1);
                        inferenceSampleNs += stageNow() - before;
                        if (!inference.pixels()) continue;
                        if (poseBatchSize <= 1) {
                            PendingPoseSample sample{frame.timestampSeconds, inference.width,
                                                     inference.height, poseSampleIndex,
                                                     std::move(inference.rgba), inference.borrowed};
                            pfgpu::PoseImage image{sample.width, sample.height, sample.pixels()};
                            const auto stageStart = stageNow();
                            const auto detections = pose->infer(image);
                            poseNs += stageNow() - stageStart;
                            processPose(sample, detections);
                        } else {
                            PendingPoseSample sample{frame.timestampSeconds, inference.width,
                                                     inference.height, poseSampleIndex,
                                                     std::move(inference.rgba)};
                            pendingPose.push_back(std::move(sample));
                            if (pendingPose.size() >= poseBatchSize) flushPoseBatch();
                        }
                    }
                    if (!frame.rgba.empty()) lastFrame = std::move(frame);
                }
                sampleReader.finish(); // decoder diagnostics are safe only after join
                if (profilePipeline) {
                    const auto waits = sampleReader.waitDiagnostics();
                    std::fprintf(stderr, "PF_DEBUG_TIMING consumer_queue_wait_ms=%.3f producer_queue_wait_ms=%.3f consumer_waits=%llu producer_waits=%llu inference_sample_ms=%.3f scene_thumbnail_ms=%.3f batch_slots=%zu\n",
                        waits.consumerMilliseconds, waits.producerMilliseconds,
                        static_cast<unsigned long long>(waits.consumerWaits), static_cast<unsigned long long>(waits.producerWaits),
                        inferenceSampleNs / 1e6, sceneThumbnailNs / 1e6, requestedPoseBatch);
                }
                if (!isCancelled()) flushPoseBatch();
                const auto decodeAndInferNs = stageNow();
                if (qEnvironmentVariableIsSet("PF_DEBUG_ANALYSIS")) {
                    const auto stats = decoder.diagnostics();
                    std::fprintf(stderr, "PF_DECODE backend=%s threads=%d cache_hit=%d decoded=%llu converted=%llu downloads=%llu read_ms=%.1f conversion_ms=%.1f fallback=%s\n",
                        stats.backend.c_str(), stats.threads, cacheHit ? 1 : 0,
                        static_cast<unsigned long long>(stats.decodedFrames),
                        static_cast<unsigned long long>(stats.convertedFrames),
                        static_cast<unsigned long long>(stats.hardwareDownloads),
                        stats.readMilliseconds, stats.conversionMilliseconds, stats.fallbackReason.c_str());
                }
                markCancelled();
                if (!error.isEmpty()) break;
                std::vector<pfcore::SceneSample> samples;
                samples.reserve(sceneBuffers.size());
                for (std::size_t sample = 0; sample < sceneBuffers.size(); ++sample)
                    samples.push_back({sceneTimestamps[sample], 64, 36, sceneBuffers[sample]});
                pfcore::SceneDetector sceneDetector(settings.sceneThreshold,
                                                    std::max<std::size_t>(1,
                                                        static_cast<std::size_t>(std::ceil(
                                                            static_cast<double>(settings.sceneMinFrames)
                                                            * 4.0 / std::max(1.0, info.frameRate)))),
                                                    settings.sceneAdaptiveMultiplier);
                const auto sceneBoundaries = sceneDetector.detect(samples);
                const auto sceneDetectNs = stageNow() - decodeAndInferNs;
                if (!cacheHit && !sceneBoundaries.empty()) {
                    std::vector<double> cuts;
                    cuts.reserve(sceneBoundaries.size());
                    for (const auto& cut : sceneBoundaries) cuts.push_back(cut.timestampSeconds);
                    const auto trackingStart = stageNow();
                    tracker.retrackScenes(cuts);
                    rc16Tracker.retrackScenes(cuts);
                    trackerNs += stageNow() - trackingStart;
                }
                const auto sourceSelection = cacheHit ? pfcore::DominantSourceSelection{}
                    : pfcore::selectDominantSourceTracks(tracker.tracks(), &rc16Tracker.tracks());
                const auto& preferredTracks = sourceSelection.tracks;
                // Opt-in identity audit: preserve independently sampled crops
                // before scene prototypes hide track switches or mixed faces.
                // No allocations or observation traversal in normal analysis.
                const QString appearanceAuditPath = qEnvironmentVariable("PF_DEBUG_APPEARANCE_JSON");
                if (!appearanceAuditPath.isEmpty() && !cacheHit) {
                    QJsonArray observations;
                    const auto vectorJson = [](const std::vector<float>& values) {
                        QJsonArray array;
                        for (const auto value : values) array.append(value);
                        return array;
                    };
                    for (std::size_t trackIndex = 0; trackIndex < tracker.tracks().size(); ++trackIndex) {
                        const auto& track = tracker.tracks()[trackIndex];
                        for (const auto& observation : track.observations) {
                            if (observation.faceEmbedding.empty() && observation.appearanceEmbedding.empty()) continue;
                            QJsonArray points;
                            for (const auto& point : observation.keypoints)
                                points.append(QJsonArray{point.x, point.y, point.confidence});
                            observations.append(QJsonObject{
                                {"track", static_cast<qint64>(track.id)},
                                {"sourceLead", preferredTracks[trackIndex]},
                                {"time", observation.timestampSeconds},
                                {"box", QJsonArray{observation.box.left, observation.box.top,
                                                    observation.box.right, observation.box.bottom}},
                                {"points", points},
                                {"face", vectorJson(observation.faceEmbedding)},
                                {"body", vectorJson(observation.appearanceEmbedding)}});
                        }
                    }
                    const QString destination = paths.size() == 1 ? appearanceAuditPath
                        : appearanceAuditPath + QStringLiteral(".%1.json").arg(files);
                    if (QFileInfo::exists(destination)) {
                        qWarning("Identity audit refuses to overwrite existing observations.");
                    } else {
                        QSaveFile diagnosticFile(destination);
                        if (diagnosticFile.open(QIODevice::WriteOnly)) {
                            const auto bytes = QJsonDocument(QJsonObject{{"source", path},
                                {"policy", QStringLiteral("Diagnostic model observations, not independent ground truth")},
                                {"observations", observations}}).toJson(QJsonDocument::Compact);
                            if (diagnosticFile.write(bytes) != bytes.size()) {
                                diagnosticFile.cancelWriting();
                                qWarning("Cannot write complete identity audit.");
                            } else if (!diagnosticFile.commit()) qWarning("Cannot commit identity audit.");
                        } else qWarning("Cannot open identity audit output.");
                    }
                }
                // Opt-in raw track evidence, before any window selection. This
                // is diagnostic data, never identity ground truth.
                const QString trackAuditPath = qEnvironmentVariable("PF_DEBUG_TRACKS_JSON");
                if (!trackAuditPath.isEmpty() && !cacheHit) {
                    QJsonArray tracks;
                    const auto vectorJson = [](const std::vector<float>& values) {
                        QJsonArray array;
                        for (const auto value : values) array.append(value);
                        return array;
                    };
                    for (std::size_t i = 0; i < tracker.tracks().size(); ++i) {
                        const auto& track = tracker.tracks()[i];
                        QJsonArray observations;
                        for (const auto& observation : track.observations) {
                            QJsonArray points;
                            for (const auto& point : observation.keypoints)
                                points.append(QJsonArray{point.x, point.y, point.confidence});
                            observations.append(QJsonObject{
                                {"time", observation.timestampSeconds},
                                {"duration", observation.frameDurationSeconds},
                                {"confidence", observation.confidence},
                                {"keypointConfidence", observation.keypointConfidence},
                                {"appearanceConfidence", observation.appearanceConfidence},
                                {"box", QJsonArray{observation.box.left, observation.box.top,
                                                    observation.box.right, observation.box.bottom}},
                                {"points", points}, {"face", vectorJson(observation.faceEmbedding)},
                                {"body", vectorJson(observation.appearanceEmbedding)}});
                        }
                        QJsonArray runs;
                        for (const auto range : sourceSelection.observationRuns[i])
                            runs.append(QJsonArray{static_cast<qint64>(range.begin), static_cast<qint64>(range.end)});
                        tracks.append(QJsonObject{{"id", static_cast<qint64>(track.id)},
                            {"sourceLead", preferredTracks[i]}, {"recovered", sourceSelection.recovered[i]},
                            {"allowedRuns", runs}, {"observations", observations}});
                    }
                    const QString destination = paths.size() == 1 ? trackAuditPath
                        : trackAuditPath + QStringLiteral(".%1.json").arg(files);
                    if (!QFileInfo::exists(destination)) {
                        QSaveFile diagnosticFile(destination);
                        if (diagnosticFile.open(QIODevice::WriteOnly)) {
                            const auto bytes = QJsonDocument(QJsonObject{{"source", path},
                                {"schema", 2}, {"tracks", tracks}}).toJson(QJsonDocument::Compact);
                            if (diagnosticFile.write(bytes) == bytes.size()) diagnosticFile.commit();
                        }
                    } else qWarning("Tracker audit refuses to overwrite existing observations.");
                }
                // A boundary separates two scenes; the user-facing counter is
                // the number of actual segments, not the number of cuts.
                scenes += samples.empty() ? 0 : static_cast<int>(sceneBoundaries.size() + 1U);
                auto summary = summaries.value(path).toMap();
                summary.insert("sceneCount", samples.empty() ? 0 : static_cast<int>(sceneBoundaries.size() + 1U));
                summaries.insert(path, summary);
                const QString previewStart = firstFrame.rgba.empty() ? QString()
                    : savePreview(firstFrame, QStringLiteral("%1_%2_start.png").arg(previewToken).arg(files));
                const QString previewEnd = lastFrame.rgba.empty() ? QString()
                    : savePreview(lastFrame, QStringLiteral("%1_%2_end.png").arg(previewToken).arg(files));
                if (qEnvironmentVariableIsSet("PF_DEBUG_ANALYSIS")) {
                    for (const auto& boundary : sceneBoundaries)
                        std::fprintf(stderr, "PF_DEBUG_SCENE cut=%.6f\n", boundary.timestampSeconds);
                }
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
                        if (isCancelled()) { markCancelled(); break; }
                        const double sceneStart = sceneStarts[sceneIndex];
                        const double sceneEnd = sceneIndex + 1 < sceneStarts.size()
                            ? sceneStarts[sceneIndex + 1] : std::max(info.durationSeconds, lastTimestamp + 0.001);
                        // Restrict every shot to the source's lead identity.
                        // An absent lead must not promote an interlocutor;
                        // pairwise same-person checks cannot catch extra/extra.
                        const auto dominantTracks = pfcore::selectDominantSceneTracks(
                            tracker.tracks(), sceneStart, sceneEnd, preferredTracks);
                        std::vector<const pfcore::PersonTrack*> sceneTracks;
                        for (std::size_t trackIndex = 0;
                             trackIndex < tracker.tracks().size(); ++trackIndex) {
                            if (trackIndex >= dominantTracks.size() || !dominantTracks[trackIndex])
                                continue;
                            const auto& track = tracker.tracks()[trackIndex];
                            std::size_t observationsInScene = 0;
                            for (const auto& observation : track.observations) {
                                if (observation.timestampSeconds >= sceneStart
                                    && observation.timestampSeconds < sceneEnd) {
                                    ++observationsInScene;
                                }
                            }
                            if (observationsInScene >= 2) sceneTracks.push_back(&track);
                        }
                        std::sort(sceneTracks.begin(), sceneTracks.end(),
                                  [&](const auto* left, const auto* right) {
                            auto sceneDuration = [&](const auto* track) {
                                double total = 0.0;
                                for (const auto& observation : track->observations) {
                                    if (observation.timestampSeconds >= sceneStart
                                        && observation.timestampSeconds < sceneEnd) {
                                        total += observation.frameDurationSeconds;
                                    }
                                }
                                return total;
                            };
                            const double leftDuration = sceneDuration(left);
                            const double rightDuration = sceneDuration(right);
                            if (std::abs(leftDuration - rightDuration) > 1e-9)
                                return leftDuration > rightDuration;
                            if (std::abs(left->averageArea() - right->averageArea()) > 1e-9)
                                return left->averageArea() > right->averageArea();
                            return left->id < right->id;
                        });
                        // Preserve separate track fragments of the selected
                        // identity; occlusion may split one gesture across IDs.
                        for (const auto* trackPtr : sceneTracks) {
                            const auto& track = *trackPtr;
                            if (isCancelled()) { markCancelled(); break; }
                            const auto trackIndex = static_cast<std::size_t>(trackPtr - tracker.tracks().data());
                            for (const auto observationRun : sourceSelection.observationRuns[trackIndex]) {
                                const bool bounded = sourceSelection.recovered[trackIndex]
                                    || observationRun.begin != 0 || observationRun.end != track.observations.size();
                                const double supportedStart = bounded
                                    ? std::max(sceneStart, track.observations[observationRun.begin].timestampSeconds) : sceneStart;
                                const double supportedEnd = bounded
                                    ? std::min(sceneEnd, track.observations[observationRun.end-1].timestampSeconds + 1e-6) : sceneEnd;
                                if (supportedEnd <= supportedStart) continue;
                                pfcore::MotionWindow window;
                                window.sourceId = path.toStdString();
                                window.trackId = track.id;
                                window.sceneIndex = sceneIndex;
                                window.hasSceneIndex = true;
                                window.sceneStartSeconds = supportedStart;
                                window.sceneEndSeconds = supportedEnd;
                                const auto appearance = averageAppearance(track, supportedStart, supportedEnd);
                                window.appearanceEmbedding = appearance.embedding;
                                window.appearanceConfidence = appearance.confidence;
                                const auto facePrototype = averageAppearance(track, supportedStart, supportedEnd, true);
                                window.faceEmbedding = facePrototype.embedding;
                                window.faceConfidence = facePrototype.confidence;
                                for (const auto& observation : track.observations) {
                                    if (observation.timestampSeconds >= supportedStart
                                        && observation.timestampSeconds < supportedEnd) {
                                        window.frames.push_back({observation.timestampSeconds, observation.keypoints});
                                    }
                                }
                                // Never reduce a scene to one representative frame. Each
                                // selected analysis mode creates a temporal set of fresh
                                // samples; the matcher can then enforce its multi-frame
                                // support and motion/static policy.
                                const auto appendChunks = [&](double windowSeconds,
                                                              double strideSeconds,
                                                              double minimumWindowSeconds,
                                                              bool staticFrameSet,
                                                              std::size_t first = 0,
                                                              std::size_t stop = std::numeric_limits<std::size_t>::max()) {
                                    const auto limit = std::min(stop, window.frames.size());
                                    if (first >= limit || limit - first < 2) return;
                                    std::size_t start = first;
                                    while (start < limit) {
                                        if (isCancelled()) { markCancelled(); break; }
                                        const double startTime = window.frames[start].timestampSeconds;
                                        if (startTime + minimumWindowSeconds > sceneEnd + 1e-9) break;
                                        std::size_t end = start;
                                        while (end + 1 < limit
                                               && window.frames[end + 1].timestampSeconds
                                                   <= startTime + windowSeconds + 1e-9
                                               && window.frames[end + 1].timestampSeconds
                                                   - window.frames[end].timestampSeconds <= 0.5) {
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
                                            chunk.staticFrameSet = staticFrameSet;
                                            chunk.sceneStartSeconds = window.sceneStartSeconds;
                                            chunk.sceneEndSeconds = window.sceneEndSeconds;
                                            chunk.appearanceEmbedding = window.appearanceEmbedding;
                                            chunk.appearanceConfidence = window.appearanceConfidence;
                                            chunk.faceEmbedding = window.faceEmbedding;
                                            chunk.faceConfidence = window.faceConfidence;
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
                                        while (next < limit
                                               && window.frames[next].timestampSeconds < nextTime) {
                                            ++next;
                                        }
                                        start = next;
                                    }
                                };
                                if (analysisMode == QStringLiteral("motion")) {
                                    appendChunks(2.5, 0.75, 0.75, false);
                                } else if (analysisMode == QStringLiteral("static")) {
                                    appendChunks(2.0, 0.75, pfcore::MotionMatcherParams::minimumStaticSpanSeconds, true);
                                } else { // combined: independent motion and static passes
                                    appendChunks(2.5, 0.75, 0.75, false);
                                    appendChunks(2.0, 0.75, pfcore::MotionMatcherParams::minimumStaticSpanSeconds, true);
                                }
                                if (analysisMode != QStringLiteral("motion")) {
                                    for (const auto range : pfcore::observedPoseRuns(window)) {
                                        if (range.begin == 0 && range.end + 1 == window.frames.size()) continue;
                                        appendChunks(2.0, 0.75, pfcore::MotionMatcherParams::minimumStaticSpanSeconds,
                                                     true, range.begin, range.end + 1);
                                    }
                                }
                            }
                        }
                    }
                }
                // Version-7 migration or a cold pass: derive scene context
                // once. Version 8 stores it with the expensive pose windows.
                const pfcore::SceneContextIndex contextIndex(samples);
                for (auto& candidate : windows) {
                    if (candidate.sourceId == path.toStdString() && !candidate.frames.empty())
                        candidate.sceneContext = contextIndex.query(
                            candidate.frames.front().timestampSeconds, candidate.frames.back().timestampSeconds);
                }
                std::vector<pfcore::MotionWindow> fileWindows;
                for (const auto& candidate : windows)
                    if (candidate.sourceId == path.toStdString()) fileWindows.push_back(candidate);
                ensureSceneViews(fileWindows, decoder, isCancelled);
                ensureSceneSequences(fileWindows,path.toStdString(),cacheKey,analysisCache.get(),false,isCancelled,&samples);
                if (isCancelled()) { markCancelled(); break; }
                std::unordered_map<std::size_t, const pfcore::MotionWindow*> views;
                for (const auto& candidate : fileWindows) if (candidate.sceneViewSampled)
                    views.try_emplace(candidate.sceneIndex, &candidate);
                for (auto& candidate : windows) if (candidate.sourceId == path.toStdString()) {
                    if (const auto found = views.find(candidate.sceneIndex); found != views.end()) {
                        candidate.sceneView = found->second->sceneView;
                        candidate.sceneViewSampled = true;
                    }
                }
                const auto windowsReadyNs = stageNow();
                if (analysisCache && !isCancelled() && faceFailure.isEmpty() && reidFailure.isEmpty()) {
                    std::string cacheError;
                    analysisCache->putForSource(path.toStdString(),cacheKey, serializeMotionWindows(fileWindows,
                        static_cast<int>(sceneBoundaries.size()) + 1), cacheError);
                }
                if (profilePipeline)
                    std::fprintf(stderr, "PF_DEBUG_TIMING cold_pipeline_ms=%.1f decode_infer_ms=%.1f pose_ms=%.1f face_ms=%.1f reid_ms=%.1f tracker_ms=%.1f scene_ms=%.1f windows_ms=%.1f cache_write_ms=%.1f pose_samples=%zu tracks=%zu prefetch=%d peak_buffered=%zu\n",
                        stageNow() / 1e6, decodeAndInferNs / 1e6, poseNs / 1e6, faceNs / 1e6,
                        reidNs / 1e6, trackerNs / 1e6, sceneDetectNs / 1e6,
                        (windowsReadyNs - decodeAndInferNs - sceneDetectNs) / 1e6,
                        (stageNow() - windowsReadyNs) / 1e6, poseSampleIndex, tracker.tracks().size(),
                        prefetchFrames ? 1 : 0, sampleReader.peakBufferedSamples());
            } catch (const std::exception& exception) {
                error = QString::fromUtf8(exception.what());
                break;
            }
        }
        markCancelled();
        if (error.isEmpty() && windows.size() >= 2 && !isCancelled()) {
            const auto sharedIdentity = pfcore::selectDominantVideoWindows(windows);
            if (paths.size() > 1) {
                std::vector<pfcore::MotionWindow> admitted;
                QStringList admittedA, admittedB;
                for (std::size_t i=0;i<windows.size();++i) if (sharedIdentity.windows[i]) {
                    admitted.push_back(std::move(windows[i]));
                    admittedA.push_back(previewA[static_cast<qsizetype>(i)]);
                    admittedB.push_back(previewB[static_cast<qsizetype>(i)]);
                }
                if (qEnvironmentVariableIsSet("PF_DEBUG_ANALYSIS")) {
                    std::fprintf(stderr,"PF_DEBUG_SHARED_IDENTITY supplied=%zu selected=%zu windows_before=%zu windows_after=%zu\n",
                        sharedIdentity.suppliedSources,sharedIdentity.sources.size(),windows.size(),admitted.size());
                    for (const auto& source:sharedIdentity.sources)
                        std::fprintf(stderr,"PF_DEBUG_SHARED_SOURCE %s\n",source.c_str());
                }
                windows=std::move(admitted);previewA=std::move(admittedA);previewB=std::move(admittedB);
                sharedIdentityStatus = QStringLiteral(" · Один человек в %1 из %2 видео")
                    .arg(sharedIdentity.sources.size()).arg(paths.size());
            }
            {
                // Cached pose windows from earlier releases also need measured
                // footage when only one source survives identity admission.
                QMetaObject::invokeMethod(this,[this,cancel] {
                    if (analysisCancel_==cancel && !cancel->load())
                        setStatus(QStringLiteral("Проверяем повторяющиеся ракурсы…"));
                },Qt::QueuedConnection);
                std::unordered_set<std::string> admittedSources;
                for (const auto& window:windows) admittedSources.insert(window.sourceId);
                for (const auto& source:admittedSources) {
                    ensureSceneSequences(windows,source,sequenceCacheKeys.at(source),analysisCache.get(),
                        providerChoice==QStringLiteral("cuda") || providerChoice==QStringLiteral("tensorrt"),isCancelled);
                    if (isCancelled()) {markCancelled();break;}
                }
            }
            if (expandedSearch) {
                // Separate cacheable observations from the current search
                // policy. Repeat analysis has already refreshed the source
                // namespace and inferred every video again above.
                std::unordered_set<std::string> ranges;
                const auto rangeKey = [](const pfcore::MotionWindow& w) {
                    return w.sourceId + "|" + std::to_string(w.trackId) + "|"
                        + std::to_string(w.sceneIndex) + "|" + std::to_string(w.staticFrameSet) + "|"
                        + std::to_string(std::llround(w.frames.front().timestampSeconds * 1000000)) + "|"
                        + std::to_string(std::llround(w.frames.back().timestampSeconds * 1000000));
                };
                for (const auto& w : windows) if (!w.frames.empty()) ranges.insert(rangeKey(w));
                const auto originalCount = windows.size();
                for (std::size_t i = 0; i < originalCount && !isCancelled(); ++i) {
                    for (auto& chunk : pfcore::shortPoseWindows(windows[i])) {
                        if (!ranges.insert(rangeKey(chunk)).second) continue;
                        windows.push_back(std::move(chunk));
                        previewA.push_back(QString()); previewB.push_back(QString());
                    }
                }
                if (qEnvironmentVariableIsSet("PF_DEBUG_ANALYSIS"))
                    std::fprintf(stderr, "PF_DEBUG_SEARCH original_windows=%zu extra_windows=%zu\n",
                                 originalCount, windows.size() - originalCount);
            }
            const QString diagnosticPath = qEnvironmentVariable("PF_DEBUG_POSES_JSON");
            const auto dumpWindows=[&] {if (!diagnosticPath.isEmpty()) {
                QJsonArray diagnosticWindows;
                for (const auto& window : windows) {
                    QJsonArray framesJson;
                    for (const auto& frame : window.frames) {
                        QJsonArray points;
                        for (const auto& point : frame.keypoints)
                            points.append(QJsonArray{point.x, point.y, point.confidence});
                        framesJson.append(QJsonObject{{"time", frame.timestampSeconds}, {"points", points}});
                    }
                    const auto embeddingJson = [](const std::vector<float>& embedding) {
                        QJsonArray values;
                        for (const float value : embedding) values.append(value);
                        return values;
                    };
                    diagnosticWindows.append(QJsonObject{{"source", QString::fromStdString(window.sourceId)},
                        {"static", window.staticFrameSet}, {"frames", framesJson},
                        {"scene", static_cast<qint64>(window.sceneIndex)},
                        {"sceneStart", window.sceneStartSeconds}, {"sceneEnd", window.sceneEndSeconds},
                        {"track", static_cast<qint64>(window.trackId)},
                        {"faceConfidence", window.faceConfidence}, {"bodyConfidence", window.appearanceConfidence},
                        {"face", embeddingJson(window.faceEmbedding)},
                        {"body", embeddingJson(window.appearanceEmbedding)},
                        {"context", embeddingJson(window.sceneContext)},
                        {"view", embeddingJson(window.sceneView)}, {"viewSampled", window.sceneViewSampled},
                        {"sequence",embeddingJson(window.sceneSequence)},
                        {"sequencePts",[&window] {QJsonArray values;for (const auto t:window.sceneSequenceTimes)values.append(t);return values;}()},
                        {"measuredFaces",[&window] {QJsonArray values;for (const auto& f:window.measuredFaces) {
                            values.append(QJsonObject{{"time",f.timestampSeconds},{"observed",f.observed},{"similarity",f.similarity},{"eyeSpan",f.relativeEyeSpan}});}return values;}()}});
                }
                QSaveFile diagnosticFile(diagnosticPath);
                if (diagnosticFile.open(QIODevice::WriteOnly)) {
                    const auto bytes = QJsonDocument(diagnosticWindows).toJson(QJsonDocument::Compact);
                    if (diagnosticFile.write(bytes) == bytes.size()) diagnosticFile.commit();
                }
            }};
            pfcore::MotionMatcherParams params;
            params.individualPairs = true;
            params.recoverUnusedShots = true;
            params.coverageSeedLimit = similarityThreshold<=.70 ? 50U : similarityThreshold>=.84 ? 200U : 100U;
            bool reuseOverrideValid = false;
            const int reuseOverride = qEnvironmentVariableIntValue("PF_RESULTS_PER_SHOT", &reuseOverrideValid);
            if (reuseOverrideValid && reuseOverride >= 0 && reuseOverride <= 100) {
                params.maxResultsPerShot = static_cast<std::size_t>(reuseOverride);
                params.individualPairs = false; // explicit diagnostic override
            }
            params.similarityThreshold = similarityThreshold;
            params.candidateThreshold = candidateThreshold;
            params.minRepeatGapSec = repeatGap;
            params.sameSourceGapFloorSec = 12.0;
            params.sameFileGapSec = sameFileGap;
            params.crossFileGapSec = crossFileGap;
            params.duplicateWindowSec = duplicateWindow;
            params.noiseFactor = noiseFactor;
            params.maxUniqueResults = static_cast<std::size_t>(maxUniqueResults);
            params.maxComparisonThreads = settings.processingThreads == 0 ? 0
                : std::min<std::size_t>(settings.processingThreads, 4);
            params.expandedSearch = expandedSearch;
            params.timeWeight = timeWeight;
            params.minTemporalFrames = qualityProfile == QStringLiteral("fast") ? 6U
                : qualityProfile == QStringLiteral("medium") ? 8U : 10U;
            params.allowStaticFrames = analysisMode == QStringLiteral("static")
                || analysisMode == QStringLiteral("combined");
            params.normalizeSize = normalizeSize;
            params.mirrorInvariant = mirrorPoses;
            params.staticArticulationSimilarityThreshold = std::clamp(similarityThreshold + 0.08, 0.82, 0.96);
            // Same-person evidence is mandatory; missing identity data must
            // never silently admit a different actor into the results.
            params.requireAppearance = true;
            // OSNet is the identity gate, not a cosmetic label. The compact
            // export used by the desktop build loses roughly 10–15 points of
            // cosine similarity across cuts, lighting and profile views. A
            // .76 floor keeps the same actor linkable while the motion score,
            // temporal run and direction gates reject look-alike actors. ReID
            // never inflates the percentage shown in the result rail.
            params.minAppearanceSimilarity = 0.76;
            params.appearanceWeight = 0.0;
            MatchedFaceVerifier faceVerifier(windows,sequenceCacheKeys,analysisCache.get(),face,
                providerChoice==QStringLiteral("cuda") || providerChoice==QStringLiteral("tensorrt"),isCancelled);
            QElapsedTimer resultTimer;
            resultTimer.start();
            foundMatches = pfcore::MotionMatcher(params).findAllPairs(windows);
            while (!isCancelled() && faceVerifier.verify(windows,foundMatches)>0) {
                QMetaObject::invokeMethod(this,[this,cancel] {
                    if(analysisCancel_==cancel && !cancel->load())setStatus(QStringLiteral("Проверяем человека в найденных фрагментах…"));
                },Qt::QueuedConnection);
                foundMatches=pfcore::MotionMatcher(params).findAllPairs(windows);
            }
            if (qEnvironmentVariableIsSet("PF_DEBUG_ANALYSIS"))
                std::fprintf(stderr, "PF_DEBUG_TIMING matcher_ms=%lld\n", static_cast<long long>(resultTimer.restart()));
            pfcore::MotionRanker::rank(foundMatches, windows);
            dumpWindows();
            if (qEnvironmentVariableIsSet("PF_DEBUG_ANALYSIS"))
                std::fprintf(stderr, "PF_DEBUG_TIMING rank_ms=%lld ranked_results=%zu\n",
                    static_cast<long long>(resultTimer.restart()), foundMatches.size());
            matches = static_cast<int>(foundMatches.size());
            const QStringList windowPreviewA = previewA;
            const QStringList windowPreviewB = previewB;
            previewA.clear();
            previewB.clear();
            std::size_t renderedPreviews = 0;
            std::size_t cachedPreviews = 0;
            std::unordered_map<std::string, std::string> previewFingerprints;
            std::unordered_map<std::string, std::unique_ptr<pfcore::VideoDecoder>> previewDecoders;
            // In particular, an empty warm result must not create a temporary
            // session or scan old preview folders for files it will never save.
            const QString previewDirectory = foundMatches.empty() ? QString{} : previewSessionPath();
            const auto previewKey = [&](const std::string& path, double seconds) {
                if (!std::isfinite(seconds)) return std::string{};
                auto [fingerprint, inserted] = previewFingerprints.try_emplace(path);
                if (inserted) fingerprint->second = cacheFileFingerprint(path);
                return "preview-v1|1920x1080|" + fingerprint->second + "|generation="
                    + sourceGenerations[path] + "|t="
                    + std::to_string(std::llround(std::max(0.0, seconds) * 1000000.0));
            };
            struct PreviewRequest {
                std::string source;
                double seconds;
                QString url;
                std::shared_future<QString> encoded;
            };
            std::vector<PreviewRequest> requests;
            std::vector<std::string> previewKeys;
            std::unordered_map<std::string, std::size_t> requestIndex;
            const auto requestPreview = [&](const std::string& path, double seconds) {
                const auto key = previewKey(path, seconds);
                if (key.empty()) return;
                const auto [entry, inserted] = requestIndex.try_emplace(key, requests.size());
                (void)entry;
                if (inserted) {
                    requests.push_back({path, std::max(0.0, seconds), {}, {}});
                    previewKeys.push_back(key);
                }
            };
            // Preserve first-use ordering/timestamps. Only PNG validation and
            // saving overlap; decoder ownership and all matcher data stay serial.
            for (const auto& item : foundMatches) {
                if (isCancelled()) break;
                requestPreview(item.leftSourceId, item.leftStartSeconds);
                requestPreview(item.rightSourceId, item.rightStartSeconds);
            }
            const auto previewWorkers = QThread::idealThreadCount() >= 8 ? 4U : 1U;
            const auto cachedUrls = pfservices::materializeCachedPreviews(analysisCache.get(), previewKeys,
                previewDirectory, isCancelled, previewWorkers);
            const auto cacheStageMs = resultTimer.elapsed();
            // No encoder thread is needed when every still is cached (or the
            // result set is empty). Start it only for the first decoded miss.
            std::unique_ptr<pfservices::PreviewWriter> previewWriter;
            for (std::size_t i = 0; i < requests.size() && !previewDirectory.isEmpty(); ++i) {
                if (isCancelled()) break;
                auto& request = requests[i];
                if(analysisCache) {
                    std::string ownershipError;
                    analysisCache->rememberSourceKey(request.source,previewKeys[i],ownershipError);
                }
                if (!cachedUrls[i].isEmpty()) {
                    request.url = cachedUrls[i]; ++cachedPreviews; continue;
                }
                try {
                    auto& decoder = previewDecoders[request.source];
                    if (!decoder) {
                        decoder = std::make_unique<pfcore::VideoDecoder>();
                        decoder->open(request.source);
                        decoder->setRgbaMaxDimensions(1920, 1080);
                    }
                    savePreviewAt(*decoder, request.seconds,
                        QStringLiteral("%1_match_frame_%2.png").arg(previewToken).arg(renderedPreviews++),
                        [&](const pfcore::DecodedFrame& exact, const QString& name) {
                            // QImage wraps borrowed decoder bytes; an owned copy
                            // must cross the encoder thread boundary.
                            QImage image(exact.rgba.data(), exact.width, exact.height, QImage::Format_RGBA8888);
                            if (image.width() > 1920 || image.height() > 1080)
                                image = image.scaled(1920, 1080, Qt::KeepAspectRatio, Qt::SmoothTransformation);
                            else image = image.copy();
                            const auto path = previewDirectory + QLatin1Char('/') + name;
                            if (!previewWriter)
                                previewWriter = std::make_unique<pfservices::PreviewWriter>(analysisCache.get());
                            request.encoded = previewWriter->submit(std::move(image), path, previewKeys[i]);
                            return QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded);
                        });
                } catch (...) { previewDecoders.erase(request.source); }
            }
            if (previewWriter) previewWriter->finish(); // All PNG/cache writes complete before publication.
            for (auto& request : requests)
                if (request.encoded.valid()) request.url = request.encoded.get();
            if (qEnvironmentVariableIsSet("PF_DEBUG_ANALYSIS"))
                std::fprintf(stderr, "PF_DEBUG_TIMING preview_cache_stage_ms=%lld preview_workers=%u unique_previews=%zu\n",
                    static_cast<long long>(cacheStageMs), previewWorkers, requests.size());
            pfservices::PreviewMemo exactPreviews([&](const QString& source, double seconds) {
                if (isCancelled()) return QString{};
                const auto found = requestIndex.find(previewKey(source.toStdString(), seconds));
                return found == requestIndex.end() ? QString{} : requests[found->second].url;
            });
            for (std::size_t i = 0; i < foundMatches.size(); ++i) {
                if (isCancelled()) { markCancelled(); break; }
                const auto& item = foundMatches[i];
                QVariantMap record;
                record.insert(QStringLiteral("id"), static_cast<int>(i));
                record.insert(QStringLiteral("similarity"), item.similarity);
                record.insert(QStringLiteral("direction"), QString::fromStdString(item.directionLabel));
                record.insert(QStringLiteral("gesture"), QString::fromStdString(item.gestureLabel));
                record.insert(QStringLiteral("matchType"),
                    item.leftIndex < windows.size() && windows[item.leftIndex].staticFrameSet
                        ? QStringLiteral("pose") : QStringLiteral("motion"));
                record.insert(QStringLiteral("leftSource"), QString::fromStdString(item.leftSourceId));
                record.insert(QStringLiteral("rightSource"), QString::fromStdString(item.rightSourceId));
                record.insert(QStringLiteral("appearanceSimilarity"), item.appearanceSimilarity);
                record.insert(QStringLiteral("sceneSimilarity"), item.sceneSimilarity);
                record.insert(QStringLiteral("identityVerified"), item.appearanceVerified);
                record.insert(QStringLiteral("faceVerified"), item.faceVerified);
                record.insert(QStringLiteral("headOnlyComparison"), item.headOnlyComparison);
                if (item.leftIndex < windows.size())
                    record.insert(QStringLiteral("leftTrackId"), static_cast<qulonglong>(windows[item.leftIndex].trackId));
                if (item.rightIndex < windows.size())
                    record.insert(QStringLiteral("rightTrackId"), static_cast<qulonglong>(windows[item.rightIndex].trackId));
                record.insert(QStringLiteral("leftStart"), item.leftStartSeconds);
                record.insert(QStringLiteral("leftEnd"), item.leftEndSeconds);
                record.insert(QStringLiteral("rightStart"), item.rightStartSeconds);
                record.insert(QStringLiteral("rightEnd"), item.rightEndSeconds);
                const auto leftClip = pfcore::parallelClipRange(item.leftStartSeconds, item.leftEndSeconds,
                    item.leftSceneStartSeconds, item.leftSceneEndSeconds);
                const auto rightClip = pfcore::parallelClipRange(item.rightStartSeconds, item.rightEndSeconds,
                    item.rightSceneStartSeconds, item.rightSceneEndSeconds);
                record.insert(QStringLiteral("leftClipStart"), leftClip.start);
                record.insert(QStringLiteral("leftClipEnd"), leftClip.end);
                record.insert(QStringLiteral("rightClipStart"), rightClip.start);
                record.insert(QStringLiteral("rightClipEnd"), rightClip.end);
                record.insert(QStringLiteral("leftSceneStart"), item.leftSceneStartSeconds);
                record.insert(QStringLiteral("leftSceneEnd"), item.leftSceneEndSeconds);
                record.insert(QStringLiteral("rightSceneStart"), item.rightSceneStartSeconds);
                record.insert(QStringLiteral("rightSceneEnd"), item.rightSceneEndSeconds);
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
                const QString exactA = exactPreviews.get(QString::fromStdString(item.leftSourceId), item.leftStartSeconds);
                const QString exactB = exactPreviews.get(QString::fromStdString(item.rightSourceId), item.rightStartSeconds);
                previewA.push_back(exactA.isEmpty() && item.leftIndex < static_cast<std::size_t>(windowPreviewA.size())
                                       ? windowPreviewA.at(static_cast<int>(item.leftIndex)) : exactA);
                previewB.push_back(exactB.isEmpty() && item.rightIndex < static_cast<std::size_t>(windowPreviewB.size())
                                       ? windowPreviewB.at(static_cast<int>(item.rightIndex)) : exactB);
                record.insert(QStringLiteral("leftPreview"), previewA.back());
                record.insert(QStringLiteral("rightPreview"), previewB.back());
                resultRecords.back() = record;
            }
            markCancelled();
            foundMatches.resize(static_cast<std::size_t>(resultRecords.size()));
            matches = static_cast<int>(foundMatches.size());
            if (qEnvironmentVariableIsSet("PF_DEBUG_ANALYSIS"))
                std::fprintf(stderr, "PF_DEBUG_TIMING previews_ms=%lld rendered_previews=%zu cached_previews=%zu\n",
                    static_cast<long long>(resultTimer.elapsed()), renderedPreviews, cachedPreviews);
        }
        if (qEnvironmentVariableIsSet("PF_DEBUG_ANALYSIS")) {
            qInfo().noquote() << "PF_DEBUG_ANALYSIS files=" << files
                              << "scenes=" << scenes
                              << "poseDetections=" << poseDetections
                              << "windows=" << static_cast<qulonglong>(windows.size())
                              << "matches=" << matches;
        }
        QString reidStatus = reidReady
            ? QStringLiteral("ReID: включён")
            : (reidModelPresent
                ? QStringLiteral("ReID: отключён (%1)").arg(reidFailure.isEmpty()
                    ? QStringLiteral("ошибка модели") : reidFailure)
                : QStringLiteral("ReID: модель не найдена · поиск только по движению"));
        reidStatus += face ? QStringLiteral(" · проверка лица включена")
            : (faceFailure.isEmpty() ? QStringLiteral(" · модель лица не установлена")
                                    : QStringLiteral(" · ошибка модели лица: ") + faceFailure);
        reidStatus += sharedIdentityStatus;
        const QString debugSummary = qEnvironmentVariableIsSet("PF_DEBUG_ANALYSIS")
            ? QStringLiteral(" · debug: windows=%1 detections=%2")
                .arg(static_cast<qulonglong>(windows.size()))
                .arg(poseDetections)
            : QString();
        for (auto entry = summaries.begin(); entry != summaries.end(); ++entry) {
            auto summary = entry.value().toMap();
            int count = 0;
            for (const auto& match : foundMatches)
                if (QString::fromStdString(match.leftSourceId) == entry.key()
                    || QString::fromStdString(match.rightSourceId) == entry.key()) ++count;
            summary.insert("matchCount", count);
            entry.value() = summary;
        }
        QMetaObject::invokeMethod(this, [this, cancel, files, frames, duration, scenes, poseDetections, matches, resultRecords, foundMatches, error, processedFrames, totalFramesEstimate, sourceFps, reidStatus, debugSummary, summaries, expandedSearch] {
            if (pendingInspectionPaths_) {
                busy_ = false;
                emit busyChanged();
                if (analysisCancel_ == cancel) analysisCancel_.reset();
                resumePendingInspection();
                return;
            }
            fileCount_ = files; frameCount_ = frames; durationSeconds_ = duration; sceneCount_ = scenes;
            sourceSummaries_ = summaries;
            poseDetectionCount_ = poseDetections;
            matchCount_ = matches;
            results_ = resultRecords;
            matches_ = foundMatches;
            sourceFps_ = sourceFps;
            emit summaryChanged();
            emit resultsChanged();
            analysisCompleted_ = error.isEmpty();
            emit analysisStateChanged();
            setProgress(error.isEmpty() ? 1.0 : progress_, error.isEmpty()
                ? (expandedSearch ? QStringLiteral("Повторный анализ завершён") : QStringLiteral("Анализ завершён"))
                : QStringLiteral("Анализ остановлен"), processedFrames, totalFramesEstimate);
            busy_ = false; emit busyChanged();
            if (analysisCancel_ == cancel) analysisCancel_.reset();
            setStatus(error.isEmpty() ? (expandedSearch ? QStringLiteral("Повторный анализ завершён · ")
                                                       : QStringLiteral("Анализ сцен завершён · ")) + reidStatus + debugSummary
                                      : QStringLiteral("Анализ остановлен: ") + error);
        }, Qt::QueuedConnection);
        } catch (const std::exception& exception) {
            const QString message = QString::fromUtf8(exception.what());
            QMetaObject::invokeMethod(this, [this, cancel, message] {
                busy_ = false;
                analysisCompleted_ = false;
                emit analysisStateChanged();
                emit busyChanged();
                if (analysisCancel_ == cancel) analysisCancel_.reset();
                if (resumePendingInspection()) return;
                setProgress(0.0, QStringLiteral("Ошибка анализа"), 0, 0);
                setStatus(QStringLiteral("Анализ не запущен: ") + message);
            }, Qt::QueuedConnection);
        } catch (...) {
            QMetaObject::invokeMethod(this, [this, cancel] {
                busy_ = false;
                analysisCompleted_ = false;
                emit analysisStateChanged();
                emit busyChanged();
                if (analysisCancel_ == cancel) analysisCancel_.reset();
                if (resumePendingInspection()) return;
                setProgress(0.0, QStringLiteral("Ошибка анализа"), 0, 0);
                setStatus(QStringLiteral("Анализ не запущен: неизвестная ошибка"));
            }, Qt::QueuedConnection);
        }
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void AnalysisController::stopAnalysis()
{
    if (!busy_ || !analysisCancel_) return;
    analysisCancel_->store(true, std::memory_order_relaxed);
    setStatus(QStringLiteral("Останавливаем анализ…"));
    setProgress(progress_, QStringLiteral("Останавливаем анализ"), processedFrames_, totalFrames_);
}

} // namespace pfui
