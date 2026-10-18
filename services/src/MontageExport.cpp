#include "MontageGeometry.hpp"
#include "ExportDestination.hpp"
#include <pfservices/MontageExport.hpp>
#include <pfservices/ExportQueue.hpp>
#include <pfcore/VideoDecoder.hpp>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <tuple>
#include <unordered_map>
#include <set>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace pfservices {
CutResult exportChronologicalMontage(std::vector<CutRequest> clips,
    const std::filesystem::path& output, std::stop_token stop,
    MontageProgress progress, const std::string& executable, bool preserveInputOrder,
    int maxWidth, int maxHeight)
{
    CutResult result;
    const auto cancelled = [&] {
        if (!stop.stop_requested()) return false;
        result.success = false;
        result.cancelled = true;
        result.error = "export cancelled";
        return true;
    };
    if (cancelled()) return result;
    if (clips.empty() || output.empty()) { result.error = "no montage clips or output"; return result; }
    const auto mode=clips.front().mode;
    const bool exactLossless=mode==CutMode::ExactLossless;
    const QString outputExtension = QString::fromStdWString(output.extension().wstring()).toLower();
    if (!isSupportedVideoContainer(outputExtension.toStdString())) {
        result.error = "unsupported video container; choose MP4, MOV, MKV, or WebM";
        return result;
    }
    if (maxWidth < 0 || maxHeight < 0 || maxWidth == 1 || maxHeight == 1
        || maxWidth > 32768 || maxHeight > 32768) {
        result.error = "invalid maximum output dimensions";
        return result;
    }
    if (exactLossless && (outputExtension != ".mov" || maxWidth || maxHeight)) {
        result.error="exact lossless montage requires MOV without a resolution limit";
        return result;
    }
    const bool streamCopy = clips.front().mode == CutMode::Fast;
    if (streamCopy && (maxWidth || maxHeight)) {
        result.error = "resolution cap requires exact re-encoding";
        return result;
    }
    if (std::any_of(clips.begin(), clips.end(), [mode](const auto& clip) {
        return clip.mode != mode;
    })) { result.error = "montage clips must use the same cut mode"; return result; }
    if (!preserveInputOrder) std::stable_sort(clips.begin(), clips.end(), [](const auto& a, const auto& b) {
        return std::tie(a.startSeconds, a.inputPath, a.endSeconds)
            < std::tie(b.startSeconds, b.inputPath, b.endSeconds);
    });
    std::set<std::tuple<std::filesystem::path, double, double>> seen;
    std::erase_if(clips, [&](const auto& clip) { return !seen.emplace(clip.inputPath, clip.startSeconds, clip.endSeconds).second; });
    const QString destination = QString::fromStdWString(output.wstring());
    const QString parent = QFileInfo(destination).absolutePath();
    if (!QDir().mkpath(parent)) { result.error = "cannot create montage folder"; return result; }
    for (const auto& clip : clips) {
        std::error_code error;
        if (std::filesystem::equivalent(clip.inputPath, output, error)) {
            result.error = "montage output must not replace an input video";
            return result;
        }
    }
    QTemporaryDir staging(parent + "/.parallelfinder-montage-XXXXXX");
    if (!staging.isValid()) { result.error = "cannot create montage staging"; return result; }
    try {
        std::unordered_map<std::string, pfcore::VideoInfo> metadata;
        const auto info = [&](const std::filesystem::path& path) -> const pfcore::VideoInfo& {
            const auto key = QString::fromStdWString(path.wstring()).toUtf8().toStdString();
            auto it = metadata.find(key);
            if (it == metadata.end()) {
                pfcore::VideoDecoder decoder;
                decoder.open(key);
                it = metadata.emplace(key, decoder.info()).first;
            }
            return it->second;
        };
        const auto first = info(clips.front().inputPath);
        if (streamCopy || exactLossless) {
            for (const auto& clip : clips) {
                if (cancelled()) return result;
                const auto& current = info(clip.inputPath);
                // Exact lossless re-encodes decoded pixels into a common
                // lossless codec. Different source codecs/extradata are not a
                // reason to reject it; verify the prepared streams below.
                const bool incompatible = streamCopy
                    ? current.streamCopySignature != first.streamCopySignature
                    : current.width != first.width || current.height != first.height
                        || current.sampleAspectRatio != first.sampleAspectRatio
                        || current.rotationDegrees != first.rotationDegrees;
                if (incompatible) {
                    result.error = "Lossless montage requires compatible video and audio streams. Export separate clips or choose re-encoding quality.";
                    return result;
                }
            }
        }
        const auto canvas = detail::montageCanvas(first, maxWidth, maxHeight);
        if (!canvas) { result.error = "montage display dimensions exceed the safe limit"; return result; }
        const auto [width, height] = *canvas;
        const double fps = first.frameRate > 0.0 && std::isfinite(first.frameRate) ? first.frameRate : 25.0;
        QByteArray manifest("ffconcat version 1.0\n");
        double montageDuration = 0;
        std::string preparedSignature;
        std::vector<CutRequest> prepared(clips.size());
        std::vector<double> fractions(clips.size(), 0);
        std::vector<CutRequest> jobs;
        jobs.reserve(clips.size());
        for (std::size_t i = 0; i < clips.size(); ++i) {
            auto request = clips[i];
            const auto originalPrepare = std::move(request.prepare);
            request.prepare = [&, i, originalPrepare](CutRequest& ready) {
                if (originalPrepare) originalPrepare(ready);
                if (stop.stop_requested()) return;
                if(exactLossless && (ready.mode!=mode || ready.maxWidth || ready.maxHeight
                    || ready.canvasWidth || ready.canvasHeight || ready.outputFrameRate
                    || ready.ensureStereoAudio || ready.videoBitrateKbps))
                    throw std::runtime_error("exact lossless montage cannot resize, normalize, or limit bitrate");
                ready.outputPath = std::filesystem::path(staging.filePath(QString::number(i)
                    + outputExtension).toStdWString());
                ready.mode = mode;
                ready.maxWidth = ready.maxHeight = 0;
                ready.canvasWidth = streamCopy || exactLossless ? 0 : width;
                ready.canvasHeight = streamCopy || exactLossless ? 0 : height;
                ready.outputFrameRate = streamCopy || exactLossless ? 0 : fps;
                ready.ensureStereoAudio = !streamCopy && !exactLossless;
                ready.sourceHasAudio = info(ready.inputPath).hasAudio;
                ready.stopToken = stop;
                ready.progress = [&, i, duration = ready.endSeconds-ready.startSeconds](double seconds) {
                    fractions[i] = std::max(fractions[i], duration > 0
                        ? std::clamp(seconds / duration, 0.0, 1.0) : 0);
                    double total = 0;
                    for (const auto fraction : fractions) total += fraction;
                    const auto completed = static_cast<std::size_t>(total);
                    if (progress) progress(completed, clips.size()+1,
                        static_cast<int>((total-completed)*100));
                };
                prepared[i] = ready;
                prepared[i].prepare = {};
            };
            jobs.push_back(std::move(request));
        }
        if (progress) progress(0, clips.size()+1, 0);
        const auto batch = runExportQueue(jobs, stop, {}, executable);
        if (cancelled()) return result;
        for (std::size_t i = 0; i < batch.completed.size(); ++i) {
            result = batch.completed[i];
            if (!result.success) return result;
            const auto& request = prepared[i];
            const QString name = QString::fromStdWString(result.outputPath.wstring());
            if(exactLossless) {
                const auto signature=info(result.outputPath).streamCopyCodecSignature;
                if(i && signature!=preparedSignature) {
                    result.success=false;
                    result.error="exact lossless clips produced incompatible streams; no lossy fallback was used";
                    return result;
                }
                preparedSignature=signature;
            }
            const QString spliceName = streamCopy ? staging.filePath(QString::number(i) + "-splice" + outputExtension) : name;
            const double clipDuration = streamCopy
                ? pfcore::prepareStreamCopySplice(name.toUtf8().toStdString(), spliceName.toUtf8().toStdString(), stop)
                : (exactLossless ? request.endSeconds-request.startSeconds
                                 : pfcore::mediaPacketDuration(spliceName.toUtf8().toStdString(), stop));
            if (cancelled()) return result;
            montageDuration += clipDuration;
            manifest += "file '" + QFileInfo(spliceName).fileName().toUtf8() + "'\n";
            if(exactLossless)manifest += "inpoint 0\n";
            manifest += "duration " + QByteArray::number(clipDuration, 'f', 9) + "\n";
        }
        result.success = false;
        const QString list = staging.filePath("clips.ffconcat");
        QFile file(list);
        if (!file.open(QIODevice::WriteOnly) || file.write(manifest) != manifest.size()) {
            result.error = "cannot write montage list"; return result;
        }
        file.close();
        QString program = QString::fromStdString(executable);
        if (!QFileInfo(program).isAbsolute()) {
            const auto local = QCoreApplication::applicationDirPath() + "/" + program;
            if (QFileInfo(local).isFile()) program = local;
            else if (QFileInfo(local + ".exe").isFile()) program = local + ".exe";
            else program = QStandardPaths::findExecutable(program);
        }
        QProcess process;
        const QString combinedPath=staging.filePath(QStringLiteral("combined") + outputExtension);
        process.setProgram(program);
        QStringList muxArguments{"-hide_banner", "-loglevel", "error", "-nostdin", "-y", "-nostats", "-progress", "pipe:1",
            "-f", "concat", "-safe", "1", "-auto_convert", streamCopy ? "0" : "1", "-i", list, "-map", "0:v:0", "-map", "0:a?", "-c", "copy"};
        if (outputExtension == ".mp4" || outputExtension == ".mov")
            muxArguments << "-movflags" << "+faststart";
        muxArguments << combinedPath;
        process.setArguments(muxArguments);
        if(exactLossless) {
            auto arguments=process.arguments();arguments.prepend("-copyts");process.setArguments(arguments);
        }
#ifdef Q_OS_WIN
        process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) { args->flags |= CREATE_NO_WINDOW; });
#endif
        process.start();
        if (!process.waitForStarted(5000)) { result.error = process.errorString().toStdString(); return result; }
        QElapsedTimer timer;
        timer.start();
        QByteArray errors;
        QByteArray pendingProgress;
        qint64 lastAdvance = 0, previousSize = -1;
        int lastPercent = -1;
        const auto reportProgress = [&] {
            errors += process.readAllStandardError();
            if (errors.size() > 65536) errors = errors.right(65536);
            pendingProgress += process.readAllStandardOutput();
            qsizetype newline;
            while ((newline = pendingProgress.indexOf('\n')) >= 0) {
                const auto line = pendingProgress.left(newline).trimmed();
                pendingProgress.remove(0, newline + 1);
                if (!line.startsWith("out_time_us=")) continue;
                bool ok = false;
                const double seconds = line.mid(12).toDouble(&ok) / 1000000.0;
                const int percent = ok && montageDuration > 0
                    ? static_cast<int>(std::clamp(seconds / montageDuration, 0.0, 1.0) * 95) : 0;
                if (percent > lastPercent) {
                    lastAdvance = timer.elapsed(); lastPercent = percent;
                    if (progress) progress(clips.size(), clips.size() + 1, percent);
                }
            }
            const auto size = QFileInfo(combinedPath).size();
            if (size > previousSize) { previousSize = size; lastAdvance = timer.elapsed(); }
        };
        while (!process.waitForFinished(100)) {
            reportProgress();
            if (stop.stop_requested() || timer.elapsed() - lastAdvance > 120000) {
                process.kill(); process.waitForFinished(5000);
                if (!cancelled()) result.error = "montage mux stopped advancing";
                return result;
            }
        }
        reportProgress();
        if (cancelled()) return result;
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            result.error = "montage mux: " + errors.right(65536).toStdString(); return result;
        }
        detail::ExportDestination destinationName(output);
        QFile combined(combinedPath);
        QSaveFile finalFile(QString::fromStdWString(destinationName.path().wstring()));
        if (!combined.open(QIODevice::ReadOnly) || combined.size() == 0 || !finalFile.open(QIODevice::WriteOnly)) {
            result.error = "cannot install completed montage"; return result;
        }
        while (!combined.atEnd()) {
            if (cancelled()) return result;
            const auto bytes = combined.read(1024 * 1024);
            if (bytes.isEmpty() || finalFile.write(bytes) != bytes.size()) {
                result.error = "cannot copy completed montage"; return result;
            }
            const int percent = 95 + static_cast<int>(combined.pos() * 5 / combined.size());
            if (progress && percent != lastPercent) progress(clips.size(), clips.size() + 1, percent);
            lastPercent = percent;
        }
        if (cancelled()) return result;
        if (!finalFile.commit()) { result.error = finalFile.errorString().toStdString(); return result; }
        destinationName.commit();
        result.outputPath = destinationName.path();
        result.success = true;
        result.error.clear();
        if (progress) progress(clips.size() + 1, clips.size() + 1, 100);
    } catch (const std::exception& error) { result.success = false; if (!cancelled()) result.error = error.what(); }
    return result;
}
}
