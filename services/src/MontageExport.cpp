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
        int width = std::max(2, static_cast<int>(std::round(first.width * first.sampleAspectRatio)) / 2 * 2);
        int height = std::max(2, first.height / 2 * 2);
        if (std::abs(first.rotationDegrees) > 45.0 && std::abs(first.rotationDegrees) < 135.0)
            std::swap(width, height);
        const double fps = first.frameRate > 0.0 && std::isfinite(first.frameRate) ? first.frameRate : 25.0;
        const CutService cutter(executable);
        QByteArray manifest("ffconcat version 1.0\n");
        for (std::size_t i = 0; i < clips.size(); ++i) {
            if (cancelled()) return result;
            auto request = clips[i];
            const QString name = staging.filePath(QString::number(i) + ".mp4");
            request.outputPath = std::filesystem::path(name.toStdWString());
            request.mode = CutMode::Exact;
            request.maxWidth = request.maxHeight = 0;
            request.canvasWidth = width;
            request.canvasHeight = height;
            request.outputFrameRate = fps;
            request.ensureStereoAudio = true;
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
        process.setArguments({"-hide_banner", "-loglevel", "error", "-nostdin", "-y",
            "-f", "concat", "-safe", "1", "-i", list, "-c", "copy", "-movflags", "+faststart",
            staging.filePath("combined.mp4")});
#ifdef Q_OS_WIN
        process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) { args->flags |= CREATE_NO_WINDOW; });
#endif
        process.start();
        if (!process.waitForStarted(5000)) { result.error = process.errorString().toStdString(); return result; }
        QElapsedTimer timer;
        timer.start();
        QByteArray errors;
        while (!process.waitForFinished(100)) {
            errors += process.readAllStandardError();
            if (errors.size() > 65536) errors = errors.right(65536);
            if (stop.stop_requested() || timer.elapsed() > 120000) {
                process.kill(); process.waitForFinished(5000);
                if (!cancelled()) result.error = "montage mux timed out";
                return result;
            }
        }
        errors += process.readAllStandardError();
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
