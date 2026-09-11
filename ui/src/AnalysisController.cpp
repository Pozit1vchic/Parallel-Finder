#include "AnalysisController.h"

#include <QMetaObject>
#include <QCoreApplication>
#include <QQmlEngine>
#include <QThread>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <vector>

#include "pfcore/VideoDecoder.hpp"
#include "pfcore/SceneDetector.hpp"
#include "pfcore/MotionMatcher.hpp"
#include "pfgpu/PoseEstimator.hpp"

namespace pfui {
namespace {

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

AnalysisController::AnalysisController(QObject* parent) : QObject(parent) {}

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
    QThread* thread = QThread::create([this, paths] {
        int files = 0;
        int scenes = 0;
        int poseDetections = 0;
        int matches = 0;
        qlonglong frames = 0;
        double duration = 0.0;
        QString error;
        const auto model = findPoseModel();
        std::unique_ptr<pfgpu::PoseEstimator> pose;
        if (!model.empty()) pose = std::make_unique<pfgpu::PoseEstimator>(model.string());
        std::vector<pfcore::MotionWindow> windows;
        for (const QString& path : paths) {
            try {
                pfcore::VideoDecoder decoder;
                decoder.open(path.toStdString());
                const auto info = decoder.info();
                ++files;
                duration += info.durationSeconds;
                frames += static_cast<qlonglong>(std::max(0.0, info.durationSeconds * info.frameRate));
                std::vector<pfcore::DecodedFrame> decoded;
                pfcore::DecodedFrame frame;
                std::size_t index = 0;
                while (decoder.readNext(frame)) {
                    if ((index++ % 5U) == 0U && decoded.size() < 240U) decoded.push_back(std::move(frame));
                    if (index > 1200U) break;
                }
                std::vector<pfcore::SceneSample> samples;
                samples.reserve(decoded.size());
                for (const auto& item : decoded)
                    samples.push_back({item.timestampSeconds, item.width, item.height, item.rgba});
                scenes += static_cast<int>(pfcore::SceneDetector().detect(samples).size());
                if (pose) {
                    pfcore::MotionWindow window;
                    window.sourceId = path.toStdString();
                    for (const auto& item : decoded) {
                        pfgpu::PoseImage image{item.width, item.height, item.rgba.data()};
                        const auto detections = pose->infer(image);
                        poseDetections += static_cast<int>(detections.size());
                        if (!detections.empty()) {
                            pfcore::PoseFrame poseFrame;
                            poseFrame.timestampSeconds = item.timestampSeconds;
                            const auto& keypoints = detections.front().keypoints;
                            poseFrame.keypoints.reserve(keypoints.size() / 3);
                            for (std::size_t i = 0; i + 2 < keypoints.size(); i += 3)
                                poseFrame.keypoints.push_back({keypoints[i], keypoints[i + 1], keypoints[i + 2]});
                            window.frames.push_back(std::move(poseFrame));
                        }
                    }
                    if (window.frames.size() >= 2) windows.push_back(std::move(window));
                }
            } catch (const std::exception& exception) {
                error = QString::fromUtf8(exception.what());
                break;
            }
        }
        if (error.isEmpty() && windows.size() >= 2)
            matches = static_cast<int>(pfcore::MotionMatcher().findAllPairs(windows).size());
        QMetaObject::invokeMethod(this, [this, files, frames, duration, scenes, poseDetections, matches, error] {
            fileCount_ = files; frameCount_ = frames; durationSeconds_ = duration; sceneCount_ = scenes;
            poseDetectionCount_ = poseDetections;
            matchCount_ = matches;
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
