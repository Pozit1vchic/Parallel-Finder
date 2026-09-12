#include "AnalysisController.h"

#include <QMetaObject>
#include <QCoreApplication>
#include <QDir>
#include <QImage>
#include <QStandardPaths>
#include <QQmlEngine>
#include <QThread>

#include <algorithm>
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
#include "pfservices/SettingsStore.hpp"
#include "pfservices/ModelStore.hpp"
#include "pfservices/PfCache.hpp"

namespace pfui {
namespace {

QString savePreview(const pfcore::DecodedFrame& frame, const QString& name)
{
    if (frame.rgba.empty() || frame.width <= 0 || frame.height <= 0) return {};
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::TempLocation) + QStringLiteral("/ParallelFinder/previews");
    QDir().mkpath(directory);
    const QString path = directory + QLatin1Char('/') + name;
    QImage image(frame.rgba.data(), frame.width, frame.height, QImage::Format_RGBA8888);
    return image.copy().save(path, "PNG") ? path : QString();
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
    const std::uint32_t version = 1;
    appendBytes(output, version);
    appendBytes(output, static_cast<std::uint32_t>(windows.size()));
    for (const auto& window : windows) {
        appendBytes(output, static_cast<std::uint32_t>(window.sourceId.size()));
        output.insert(output.end(), window.sourceId.begin(), window.sourceId.end());
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
    if (!readBytes(input, offset, version) || version != 1
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
    const std::filesystem::path external = R"(D:\PF_CUDA\models\yolo26m-pose-640-b1.onnx)";
    if (std::filesystem::is_regular_file(external)) return external;
    const auto besideExecutable = std::filesystem::path(QCoreApplication::applicationDirPath().toStdWString())
        / "models" / "yolo26m-pose-640-b1.onnx";
    if (std::filesystem::is_regular_file(besideExecutable)) return besideExecutable;
    std::string settingsError;
    const pfservices::Settings settings = pfservices::SettingsStore().load(settingsError);
    if (!settings.modelPath.empty()) {
        const std::filesystem::path configured(settings.modelPath);
        if (std::filesystem::is_regular_file(configured)) return configured;
    }
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

void AnalysisController::setStatus(const QString& status)
{
    if (status_ == status) return;
    status_ = status;
    emit statusChanged();
}

void AnalysisController::inspectFiles(const QStringList& paths)
{
    if (busy_) return;
    busy_ = true;
    emit busyChanged();
    setStatus(QStringLiteral("Открываем видео…"));
    QThread* thread = QThread::create([this, paths] {
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
            setStatus(error.isEmpty() ? QStringLiteral("Файлы готовы к анализу")
                                      : QStringLiteral("Не удалось открыть файл: ") + error);
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void AnalysisController::analyzeFiles(const QStringList& paths)
{
    if (busy_) return;
    busy_ = true;
    emit busyChanged();
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
    QThread* thread = QThread::create([this, paths, similarityThreshold, candidateThreshold, repeatGap,
                                        sameFileGap, crossFileGap, duplicateWindow, noiseFactor,
                                        maxUniqueResults, timeWeight] {
        int files = 0;
        int scenes = 0;
        int poseDetections = 0;
        int matches = 0;
        QStringList resultItems;
        QStringList previewA;
        QStringList previewB;
        qlonglong frames = 0;
        double duration = 0.0;
        QString error;
        std::string settingsError;
        const pfservices::Settings settings = pfservices::SettingsStore().load(settingsError);
        (void)settingsError;
        const auto model = findPoseModel();
        if (model.empty()) {
            QMetaObject::invokeMethod(this, [this] {
                busy_ = false;
                emit busyChanged();
                setStatus(QStringLiteral("Модель поз не найдена. Укажите её в settings.json или в папке models."));
            }, Qt::QueuedConnection);
            return;
        }
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
            if (const auto provider = pfgpu::parseProvider(settings.provider); provider.has_value()) {
                poseParams.provider = *provider;
            }
            pose = std::make_unique<pfgpu::PoseEstimator>(model.string(), poseParams);
        }
        std::vector<pfcore::MotionWindow> windows;
        for (const QString& path : paths) {
            try {
                pfcore::VideoDecoder decoder;
                decoder.open(path.toStdString());
                const auto info = decoder.info();
                std::vector<pfcore::MotionWindow> cachedWindows;
                bool cacheHit = false;
                std::string cacheKey = "motion-v1|" + model.string() + "|" + path.toStdString();
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
                frames += static_cast<qlonglong>(std::max(0.0, info.durationSeconds * info.frameRate));
                std::vector<std::vector<std::uint8_t>> sceneBuffers;
                std::vector<double> sceneTimestamps;
                sceneBuffers.reserve(static_cast<std::size_t>(std::max(1.0, info.durationSeconds)) + 1U);
                sceneTimestamps.reserve(sceneBuffers.capacity());
                pfcore::DominantPersonTracker tracker;
                pfcore::DecodedFrame firstFrame;
                pfcore::DecodedFrame lastFrame;
                pfcore::DecodedFrame frame;
                std::size_t index = 0;
                double nextSceneSample = 0.0;
                double previousPoseTimestamp = -1.0;
                while (decoder.readNext(frame)) {
                    if (firstFrame.rgba.empty()) firstFrame = frame;
                    if (frame.timestampSeconds + 1e-9 >= nextSceneSample) {
                        sceneBuffers.push_back(sceneThumbnail(frame));
                        sceneTimestamps.push_back(frame.timestampSeconds);
                        do { nextSceneSample += 1.0; }
                        while (nextSceneSample <= frame.timestampSeconds + 1e-9);
                    }
                    if (pose && !cacheHit && (index++ % 5U) == 0U) {
                        pfgpu::PoseImage image{frame.width, frame.height, frame.rgba.data()};
                        const auto detections = pose->infer(image);
                        poseDetections += static_cast<int>(detections.size());
                        std::vector<pfcore::PersonDetection> frameDetections;
                        frameDetections.reserve(detections.size());
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
                            frameDetections.push_back(std::move(person));
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
                scenes += static_cast<int>(sceneBoundaries.size());
                const QString previewStart = firstFrame.rgba.empty() ? QString() : savePreview(firstFrame, QStringLiteral("%1_start.png").arg(files));
                const QString previewEnd = lastFrame.rgba.empty() ? QString() : savePreview(lastFrame, QStringLiteral("%1_end.png").arg(files));
                if (cacheHit) {
                    for (auto& cached : cachedWindows) {
                        windows.push_back(std::move(cached));
                        previewA.push_back(previewStart);
                        previewB.push_back(previewEnd);
                    }
                } else if (pose) {
                    const auto dominant = tracker.dominant();
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
                        pfcore::MotionWindow window;
                        window.sourceId = path.toStdString();
                        if (dominant.has_value()) {
                            for (const auto& observation : dominant->observations) {
                                if (observation.timestampSeconds >= sceneStart
                                    && observation.timestampSeconds < sceneEnd) {
                                    window.frames.push_back({observation.timestampSeconds, observation.keypoints});
                                }
                            }
                        }
                        // Compare overlapping motion windows rather than one
                        // aggregate window per scene. The durations and stride
                        // are the documented 3b contract, measured on actual
                        // timestamps rather than guessed frame counts.
                        constexpr double windowSeconds = 1.0;
                        constexpr double strideSeconds = 0.25;
                        constexpr double minimumWindowSeconds = 0.5;
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
                    if (analysisCache) {
                        std::vector<pfcore::MotionWindow> fileWindows;
                        const std::size_t firstWindow = windows.size();
                        (void)firstWindow;
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
            auto found = pfcore::MotionMatcher(params).findAllPairs(windows);
            pfcore::MotionRanker::rank(found, windows);
            matches = static_cast<int>(found.size());
            const QStringList windowPreviewA = previewA;
            const QStringList windowPreviewB = previewB;
            previewA.clear();
            previewB.clear();
            for (std::size_t i = 0; i < found.size(); ++i) {
                const auto& item = found[i];
                resultItems.push_back(QStringLiteral("Пара %1  ·  %2%  ·  %3  ·  %4  ·  %5 с / %6 с")
                    .arg(static_cast<int>(i + 1), 2, 10, QLatin1Char('0'))
                    .arg(static_cast<int>(item.similarity * 100.0))
                    .arg(QString::fromStdString(item.directionLabel))
                    .arg(QString::fromStdString(item.gestureLabel))
                    .arg(QString::number(item.leftStartSeconds, 'f', 1))
                    .arg(QString::number(item.rightStartSeconds, 'f', 1)));
                previewA.push_back(item.leftIndex < static_cast<std::size_t>(windowPreviewA.size()) ? windowPreviewA.at(static_cast<int>(item.leftIndex)) : QString());
                previewB.push_back(item.rightIndex < static_cast<std::size_t>(windowPreviewB.size()) ? windowPreviewB.at(static_cast<int>(item.rightIndex)) : QString());
            }
        }
        QMetaObject::invokeMethod(this, [this, files, frames, duration, scenes, poseDetections, matches, resultItems, previewA, previewB, error] {
            fileCount_ = files; frameCount_ = frames; durationSeconds_ = duration; sceneCount_ = scenes;
            poseDetectionCount_ = poseDetections;
            matchCount_ = matches;
            resultItems_ = resultItems;
            previewA_ = previewA;
            previewB_ = previewB;
            emit summaryChanged();
            busy_ = false; emit busyChanged();
            setStatus(error.isEmpty() ? QStringLiteral("Анализ сцен завершён")
                                      : QStringLiteral("Анализ остановлен: ") + error);
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

} // namespace pfui
