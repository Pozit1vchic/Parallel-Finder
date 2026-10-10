#include "MontageGeometry.hpp"
#include <pfservices/MontageExport.hpp>
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
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace pfservices {
CutResult exportChronologicalMontage(std::vector<CutRequest> clips,
    const std::filesystem::path& output, std::stop_token stop,
    MontageProgress progress, const std::string& executable)
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
    const bool streamCopy = clips.front().mode == CutMode::Fast;
    if (std::any_of(clips.begin(), clips.end(), [streamCopy](const auto& clip) {
        return (clip.mode == CutMode::Fast) != streamCopy;
    })) { result.error = "montage clips must use the same cut mode"; return result; }
    std::stable_sort(clips.begin(), clips.end(), [](const auto& a, const auto& b) {
        return std::tie(a.startSeconds, a.inputPath, a.endSeconds)
            < std::tie(b.startSeconds, b.inputPath, b.endSeconds);
    });
    clips.erase(std::unique(clips.begin(), clips.end(), [](const auto& a, const auto& b) {
        return a.inputPath == b.inputPath && a.startSeconds == b.startSeconds && a.endSeconds == b.endSeconds;
    }), clips.end());
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
        if (streamCopy) {
            for (const auto& clip : clips) {
                if (cancelled()) return result;
                if (info(clip.inputPath).streamCopySignature != first.streamCopySignature) {
                    result.error = "Lossless montage requires compatible video and audio streams. Export separate clips or choose re-encoding quality.";
                    return result;
                }
            }
        }
        const auto canvas = detail::montageCanvas(first);
        if (!canvas) { result.error = "montage display dimensions exceed the safe limit"; return result; }
        const auto [width, height] = *canvas;
        const double fps = first.frameRate > 0.0 && std::isfinite(first.frameRate) ? first.frameRate : 25.0;
        const CutService cutter(executable);
        QByteArray manifest("ffconcat version 1.0\n");
        double montageDuration = 0;
        for (std::size_t i = 0; i < clips.size(); ++i) {
            if (cancelled()) return result;
            auto request = clips[i];
            request.stopToken = stop;
            if (request.prepare) {
                auto prepare = std::move(request.prepare);
                prepare(request);
                if (cancelled()) return result;
            }
            const QString name = staging.filePath(QString::number(i) + ".mp4");
            request.outputPath = std::filesystem::path(name.toStdWString());
            request.mode = streamCopy ? CutMode::Fast : CutMode::Exact;
            request.maxWidth = request.maxHeight = 0;
            request.canvasWidth = streamCopy ? 0 : width;
            request.canvasHeight = streamCopy ? 0 : height;
            request.outputFrameRate = streamCopy ? 0 : fps;
            request.ensureStereoAudio = !streamCopy;
            request.sourceHasAudio = info(request.inputPath).hasAudio;
            request.stopToken = stop;
            if (progress) progress(i, clips.size() + 1, 0);
            request.progress = [&, i, duration = request.endSeconds - request.startSeconds, lastPercent = -1](double seconds) mutable {
                const int percent = duration > 0 ? static_cast<int>(std::clamp(seconds / duration, 0.0, 1.0) * 100) : 0;
                if (progress && percent != lastPercent) progress(i, clips.size() + 1, percent);
                lastPercent = percent;
            };
            result = cutter.cut(request);
            if (!result.success) return result;
            montageDuration += request.endSeconds - request.startSeconds;
            // Relative, generated filenames avoid quoting user paths and
            // keep concat's safe-path check enabled.
            manifest += "file '" + QByteArray::number(static_cast<qulonglong>(i)) + ".mp4'\n";
            if (progress) progress(i + 1, clips.size() + 1, 0);
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
        process.setProgram(program);
        process.setArguments({"-hide_banner", "-loglevel", "error", "-nostdin", "-y", "-nostats", "-progress", "pipe:1",
            "-f", "concat", "-safe", "1", "-auto_convert", streamCopy ? "0" : "1", "-i", list, "-map", "0:v:0", "-map", "0:a?", "-c", "copy", "-movflags", "+faststart",
            staging.filePath("combined.mp4")});
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
            const auto size = QFileInfo(staging.filePath("combined.mp4")).size();
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
        QFile combined(staging.filePath("combined.mp4"));
        QSaveFile finalFile(destination);
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
        result.success = true;
        result.error.clear();
        if (progress) progress(clips.size() + 1, clips.size() + 1, 100);
    } catch (const std::exception& error) { result.success = false; result.error = error.what(); }
    return result;
}
}
