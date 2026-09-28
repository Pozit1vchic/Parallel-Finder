#include <pfservices/CutService.hpp>

#include <QFile>
#include <QCoreApplication>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QProcess>
#include <QStandardPaths>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <system_error>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace pfservices {
namespace {

QString seconds(double value)
{
    return QString::number(value, 'f', 6);
}

std::string processError(QProcess& process)
{
    const QByteArray standardError = process.readAllStandardError();
    if (!standardError.isEmpty()) return standardError.toStdString();
    return process.errorString().toStdString();
}

QString resolveFfmpegExecutable(const std::string& configured)
{
    const QString requested = QString::fromStdString(configured);
    if (QFileInfo(requested).isAbsolute()) return requested;
    const QString appLocal = QCoreApplication::applicationDirPath()
        + QLatin1Char('/') + requested;
    if (QFileInfo(appLocal).isFile()) return appLocal;
#if defined(_WIN32)
    if (!requested.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)
        && QFileInfo(appLocal + QStringLiteral(".exe")).isFile())
        return appLocal + QStringLiteral(".exe");
#endif
    const QString fromPath = QStandardPaths::findExecutable(requested);
    return fromPath.isEmpty() ? requested : fromPath;
}

} // namespace

CutService::CutService(std::string ffmpegExecutable)
    : ffmpegExecutable_(std::move(ffmpegExecutable))
{
}

CutResult CutService::cut(const CutRequest& request) const
{
    CutResult result;
    if (ffmpegExecutable_.empty()) {
        result.error = "ffmpeg executable is empty";
        return result;
    }
    if (request.stopToken.stop_requested()) {
        result.cancelled = true;
        result.error = "export cancelled";
        return result;
    }
    if (request.inputPath.empty() || request.outputPath.empty()) {
        result.error = "input and output paths are required";
        return result;
    }
    if (!std::isfinite(request.startSeconds) || !std::isfinite(request.endSeconds)
        || request.startSeconds < 0.0 || request.endSeconds <= request.startSeconds) {
        result.error = "invalid cut time range";
        return result;
    }
    if (request.maxWidth < 0 || request.maxHeight < 0) {
        result.error = "maximum output dimensions must not be negative";
        return result;
    }
    if ((request.maxWidth > 0 && request.maxWidth < 2)
        || (request.maxHeight > 0 && request.maxHeight < 2)) {
        result.error = "maximum output dimensions must be at least 2 pixels";
        return result;
    }
    if (request.mode == CutMode::Fast && (request.maxWidth > 0 || request.maxHeight > 0)) {
        result.error = "resolution cap requires exact cut mode";
        return result;
    }
    std::error_code filesystemError;
    if (!std::filesystem::is_regular_file(request.inputPath, filesystemError)) {
        result.error = "input video does not exist: " + request.inputPath.string();
        return result;
    }
    if (std::filesystem::equivalent(request.inputPath, request.outputPath, filesystemError)) {
        result.error = "output path must differ from input path";
        return result;
    }
    const auto parent = request.outputPath.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, filesystemError);
        if (filesystemError) {
            result.error = "create output folder: " + filesystemError.message();
            return result;
        }
    }

    const double duration = request.endSeconds - request.startSeconds;
    const QString ffmpegProgram = resolveFfmpegExecutable(ffmpegExecutable_);
    result.executable = ffmpegProgram.toStdString();
    // Keep the real media extension on the temporary file.  FFmpeg selects
    // its muxer from the output suffix; `clip.mp4.part` has no known format
    // and produces "Unable to choose an output format" on Windows.
    const std::string extension = request.outputPath.extension().string();
    const std::string temporaryName = request.outputPath.stem().string()
        + ".part" + (extension.empty() ? std::string(".mp4") : extension);
    const auto temporaryPath = request.outputPath.parent_path() / temporaryName;
    std::filesystem::remove(temporaryPath, filesystemError);

    std::vector<std::string> encoders;
    if (request.mode == CutMode::Fast) encoders.emplace_back("copy");
    else {
        std::lock_guard lock(capabilitiesMutex_);
        if (!capabilities_) {
            auto capabilities = FfmpegCapabilities::probe(result.executable, request.stopToken);
            if (request.stopToken.stop_requested()) {
                result.cancelled = true;
                result.error = "export cancelled";
                return result;
            }
            capabilities_ = std::move(capabilities);
        }
        encoders = capabilities_->h264Candidates();
        if (!capabilities_->error.empty() || encoders.empty()) {
            result.error = result.executable + ": " + (capabilities_->error.empty()
                ? "no supported H.264 encoder; install an FFmpeg build with libx264"
                : capabilities_->error);
            return result;
        }
    }

    for (const auto& encoder : encoders) {
        if (request.stopToken.stop_requested()) {
            result.cancelled = true;
            result.error = "export cancelled";
            break;
        }
        QStringList arguments;
        arguments << QStringLiteral("-hide_banner") << QStringLiteral("-loglevel")
                  << QStringLiteral("error") << QStringLiteral("-y");
        if (request.mode == CutMode::Fast)
            arguments << QStringLiteral("-ss") << seconds(request.startSeconds);
        arguments << QStringLiteral("-i")
                  << QString::fromStdWString(request.inputPath.wstring());
        if (request.mode == CutMode::Exact)
            arguments << QStringLiteral("-ss") << seconds(request.startSeconds);
        // This is a video-clip export, not a container clone. Mapping every
        // stream also selects subtitles/data/attachments whose codecs may not
        // be supported by MP4 (including the "codec none" encoder failure).
        // Require the primary video, retain optional audio, omit other streams.
        arguments << QStringLiteral("-t") << seconds(duration)
                  << QStringLiteral("-map") << QStringLiteral("0:v:0")
                  << QStringLiteral("-map") << QStringLiteral("0:a?");
        if (request.mode == CutMode::Fast) {
            arguments << QStringLiteral("-c") << QStringLiteral("copy");
        } else {
            arguments << QStringLiteral("-c:v") << QString::fromStdString(encoder)
                      << QStringLiteral("-c:a") << QStringLiteral("aac")
                      << QStringLiteral("-movflags") << QStringLiteral("+faststart");
            if (encoder == "libx264")
                arguments << QStringLiteral("-crf") << QStringLiteral("18")
                          << QStringLiteral("-preset") << QStringLiteral("medium");
            if (request.maxWidth > 0 || request.maxHeight > 0) {
                // min(iw/ih, limit) prevents upscaling while preserving aspect ratio.
                const int width = request.maxWidth > 0 ? request.maxWidth : 100000;
                const int height = request.maxHeight > 0 ? request.maxHeight : 100000;
                const QString filter = QStringLiteral("scale=min(iw\\,%1):min(ih\\,%2):force_original_aspect_ratio=decrease")
                    .arg(width).arg(height);
                arguments << QStringLiteral("-vf") << filter;
            }
        }
        arguments << QString::fromStdWString(temporaryPath.wstring());
        result.encoder = encoder;
        result.arguments.clear();
        for (const auto& argument : arguments) result.arguments.push_back(argument.toStdString());

        QProcess process;
        process.setProgram(ffmpegProgram);
        process.setArguments(arguments);
#if defined(_WIN32)
        // ffmpeg is a console executable. CREATE_NO_WINDOW keeps its stderr
        // captured by QProcess instead of flashing a console window over the
        // Qt GUI when a cut is requested.
        process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments *args) {
            args->flags |= CREATE_NO_WINDOW;
        });
#endif
        process.setProcessChannelMode(QProcess::SeparateChannels);
        process.start();
        if (!process.waitForStarted(5000)) {
            result.error += result.executable + ": " + processError(process);
            return result;
        }
        QElapsedTimer elapsed;
        elapsed.start();
        while (process.state() != QProcess::NotRunning
               && !request.stopToken.stop_requested()
               && (request.timeoutMs <= 0 || elapsed.elapsed() < request.timeoutMs)) {
            process.waitForFinished(100);
        }
        if (request.stopToken.stop_requested() || process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(5000);
            result.cancelled = request.stopToken.stop_requested();
            result.error = result.cancelled ? "export cancelled"
                : "ffmpeg timed out: " + processError(process);
            std::filesystem::remove(temporaryPath, filesystemError);
            return result;
        }
        result.exitCode = process.exitCode();
        if (process.exitStatus() == QProcess::NormalExit && result.exitCode == 0
            && std::filesystem::is_regular_file(temporaryPath, filesystemError)) {
            std::filesystem::remove(request.outputPath, filesystemError);
            filesystemError.clear();
            std::filesystem::rename(temporaryPath, request.outputPath, filesystemError);
            if (!filesystemError) {
                result.success = true;
                result.encoder = encoder;
                result.error.clear();
                return result;
            }
            result.error = "move cut into place: " + filesystemError.message();
            break;
        }
        result.error += result.executable + " [" + encoder + ", exit "
            + std::to_string(result.exitCode) + "]: " + processError(process) + "\n";
        std::filesystem::remove(temporaryPath, filesystemError);
        if (request.mode == CutMode::Fast) break;
    }
    std::filesystem::remove(temporaryPath, filesystemError);
    if (result.error.empty()) result.error = "ffmpeg failed to create the cut";
    return result;
}

} // namespace pfservices
