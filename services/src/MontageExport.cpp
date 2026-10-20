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
        // Every exact montage uses one deterministic lossless encoder. Selecting
        // hardware independently per worker can produce incompatible HEVC/H.264.
        int losslessWidth = 0, losslessHeight = 0, audioRate = 0, audioChannels = 0;
        QString pixelFormat = QString::fromStdString(first.pixelFormat);
        bool mixedPixels = false, rgb = false, highDepthOrAlpha = false;
        const auto displaySize = [](const pfcore::VideoInfo& video) {
            // Expand the long-pixel axis only; never shrink source pixels.
            double w = video.width, h = video.height;
            if (video.sampleAspectRatio >= 1) w *= video.sampleAspectRatio;
            else h /= video.sampleAspectRatio;
            if (std::abs(video.rotationDegrees) > 45 && std::abs(video.rotationDegrees) < 135) std::swap(w,h);
            if (!std::isfinite(w) || !std::isfinite(h) || w < 2 || h < 2
                || w > 32768 || h > 32768 || w*h > 64.0*1024*1024)
                throw std::runtime_error("montage display dimensions exceed the safe limit");
            return std::pair<int,int>{static_cast<int>(std::ceil(w)), static_cast<int>(std::ceil(h))};
        };
        for (const auto& clip : clips) {
            if (cancelled()) return result;
            const auto& current = info(clip.inputPath);
            if (streamCopy && current.streamCopySignature != first.streamCopySignature) {
                result.error = "Fast montage requires compatible video/audio stream parameters. Choose exact/lossless or re-encoding quality.";
                return result;
            }
            if (exactLossless) {
                const auto [w,h] = displaySize(current);
                losslessWidth = std::max(losslessWidth,w); losslessHeight = std::max(losslessHeight,h);
                mixedPixels |= current.pixelFormat != first.pixelFormat;
                rgb |= current.pixelFormat.find("rgb") != std::string::npos
                    || current.pixelFormat.find("bgr") != std::string::npos;
                highDepthOrAlpha |= current.pixelFormat.find("12") != std::string::npos
                    || current.pixelFormat.find("14") != std::string::npos || current.pixelFormat.find("16") != std::string::npos
                    || current.pixelFormat.find("yuva") != std::string::npos || current.pixelFormat.find("rgba") != std::string::npos
                    || current.pixelFormat.find("bgra") != std::string::npos || current.pixelFormat.find("gbrap") != std::string::npos;
                audioRate = std::max(audioRate,current.audioSampleRate);
                audioChannels = std::max(audioChannels,current.audioChannels);
            }
        }
        losslessWidth = (losslessWidth+1)/2*2; losslessHeight = (losslessHeight+1)/2*2;
        if (exactLossless && (pixelFormat.isEmpty() || mixedPixels))
            pixelFormat = rgb ? "gbrap16le" : highDepthOrAlpha ? "yuva444p16le" : "yuv444p10le";
        if (static_cast<int64_t>(losslessWidth)*losslessHeight > 64LL*1024*1024)
            throw std::runtime_error("montage display dimensions exceed the safe limit");
        const auto canvas = detail::montageCanvas(first, maxWidth, maxHeight);
        if (!canvas) { result.error = "montage display dimensions exceed the safe limit"; return result; }
        const auto [width, height] = *canvas;
        const double fps = first.frameRate > 0.0 && std::isfinite(first.frameRate) ? first.frameRate : 25.0;
        QByteArray manifest("ffconcat version 1.0\n");
        QByteArray audioManifest("ffconcat version 1.0\n");
        double montageDuration = 0;
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
                ready.canvasWidth = streamCopy ? 0 : exactLossless ? losslessWidth : width;
                ready.canvasHeight = streamCopy ? 0 : exactLossless ? losslessHeight : height;
                ready.outputFrameRate = streamCopy || exactLossless ? 0 : fps;
                ready.ensureStereoAudio = !streamCopy && !exactLossless;
                ready.sourceHasAudio = info(ready.inputPath).hasAudio;
                if (exactLossless) {
                    ready.losslessPixelFormat = pixelFormat;
                    ready.losslessAudioRate = audioRate;
                    ready.losslessAudioChannels = audioChannels;
                    const auto [w,h] = displaySize(info(ready.inputPath));
                    ready.losslessDisplayWidth = w; ready.losslessDisplayHeight = h;
                }
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
            if (exactLossless) {
                const auto& actual = info(result.outputPath);
                if (actual.width != losslessWidth || actual.height != losslessHeight
                    || QString::fromStdString(actual.pixelFormat) != pixelFormat
                    || actual.hasAudio != (audioRate > 0)
                    || (audioRate > 0 && (actual.audioSampleRate != audioRate || actual.audioChannels != audioChannels))) {
                    result.success = false;
                    result.error = "lossless preparation did not match the common canvas/audio plan";
                    return result;
                }
            }
            const QString spliceName = streamCopy ? staging.filePath(QString::number(i) + "-splice" + outputExtension) : name;
            const double clipDuration = streamCopy
                ? pfcore::prepareStreamCopySplice(name.toUtf8().toStdString(), spliceName.toUtf8().toStdString(), stop)
                : (exactLossless ? request.endSeconds-request.startSeconds
                                 : pfcore::mediaPacketDuration(spliceName.toUtf8().toStdString(), stop));
            if (cancelled()) return result;
            montageDuration += clipDuration;
            manifest += "file '" + QFileInfo(spliceName).fileName().toUtf8() + "'\n";
            audioManifest += "file '" + QFileInfo(spliceName).fileName().toUtf8() + "'\n";
            // The video demuxer needs an explicit zero seek to preserve VFR
            // frame timestamps. Applying that same seek to MOV audio drops a
            // leading PCM packet, so exact-lossless audio uses its own list.
            if (exactLossless) manifest += "inpoint 0\n";
            manifest += "duration " + QByteArray::number(clipDuration, 'f', 9) + "\n";
            audioManifest += "duration " + QByteArray::number(clipDuration, 'f', 9) + "\n";
        }
        result.success = false;
        const QString list = staging.filePath("clips.ffconcat");
        QFile file(list);
        if (!file.open(QIODevice::WriteOnly) || file.write(manifest) != manifest.size()) {
            result.error = "cannot write montage list"; return result;
        }
        file.close();
        const QString audioList = staging.filePath("audio.ffconcat");
        if (exactLossless) {
            QFile audioFile(audioList);
            if (!audioFile.open(QIODevice::WriteOnly) || audioFile.write(audioManifest) != audioManifest.size()) {
                result.error = "cannot write lossless audio montage list"; return result;
            }
            audioFile.close();
        }
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
            "-f", "concat", "-safe", "1", "-auto_convert", streamCopy ? "0" : "1", "-i", list};
        if (exactLossless)
            muxArguments << "-f" << "concat" << "-safe" << "1" << "-auto_convert" << "1" << "-i" << audioList
                         << "-map" << "0:v:0" << "-map" << "1:a:0?" << "-c" << "copy";
        else
            muxArguments << "-map" << "0:v:0" << "-map" << "0:a?" << "-c" << "copy";
        if (outputExtension == ".mp4" || outputExtension == ".mov")
            muxArguments << "-movflags" << "+faststart";
        if (exactLossless) muxArguments << "-avoid_negative_ts" << "disabled";
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
