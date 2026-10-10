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
#include <thread>

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

void installCut(const std::filesystem::path& temporary,
                const std::filesystem::path& destination, std::error_code& error)
{
#if defined(_WIN32)
    if (std::filesystem::exists(destination, error)) {
        if (error) return;
        if (!ReplaceFileW(destination.c_str(), temporary.c_str(), nullptr, 0, nullptr, nullptr))
            error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
        return;
    }
    if (error) return;
#endif
    // POSIX rename replaces atomically; Windows requires ReplaceFileW when
    // the target exists. Never remove the previous cut before installation.
    std::filesystem::rename(temporary, destination, error);
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

std::vector<std::string> CutService::videoEncodingArguments(
    const std::string& encoder, int quality, int bitrateKbps, unsigned threads)
{
    std::vector<std::string> args;
    const auto q = std::to_string(quality);
    if (encoder == "libx264") {
        args = {"-preset", "veryfast", "-threads:v", std::to_string(threads)};
        if (bitrateKbps == 0) args.insert(args.end(), {"-crf", q});
    } else if (encoder == "h264_nvenc") {
        args = {"-preset", "p5", "-rc", "vbr"};
        if (bitrateKbps == 0) args.insert(args.end(), {"-cq", q, "-b:v", "0"});
    } else if (encoder == "h264_amf") {
        args = {"-quality", "quality", "-rc", bitrateKbps > 0 ? "vbr_peak" : "cqp"};
        if (bitrateKbps == 0) args.insert(args.end(), {"-qp_i", q, "-qp_p", q, "-qp_b", q});
    } else if (encoder == "h264_qsv") {
        args = {"-preset", "medium"};
        if (bitrateKbps == 0) args.insert(args.end(), {"-global_quality", q});
    }
    if (bitrateKbps > 0) args.insert(args.end(), {"-b:v", std::to_string(bitrateKbps) + "k"});
    return args;
}

CutResult CutService::cut(const CutRequest& inputRequest) const
{
    auto request = inputRequest;
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
    if (request.prepare) {
        auto prepare = std::move(request.prepare);
        prepare(request);
        if (request.stopToken.stop_requested()) {
            result.cancelled = true;
            result.error = "export cancelled";
            return result;
        }
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
    if (request.quality < 1 || request.quality > 51 || request.videoBitrateKbps < 0 || request.videoBitrateKbps > 500000) {
        result.error = "invalid encoding quality or bitrate";
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
    const bool montage = request.canvasWidth > 0 || request.canvasHeight > 0 || request.ensureStereoAudio;
    if (montage && (request.mode != CutMode::Exact || request.canvasWidth < 2 || request.canvasHeight < 2
        || request.canvasWidth % 2 || request.canvasHeight % 2
        || !std::isfinite(request.outputFrameRate) || request.outputFrameRate <= 0.0)) {
        result.error = "montage normalization requires an exact cut and valid even canvas/FPS";
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
        // Compiled-in hardware support does not mean this PC has that GPU.
        // Try the encoder that actually succeeded first for subsequent clips.
        const auto preferred = std::find(encoders.begin(), encoders.end(), preferredEncoder_);
        if (preferred != encoders.end()) std::rotate(encoders.begin(), preferred, preferred + 1);
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
                  << QStringLiteral("error") << QStringLiteral("-y")
                  << QStringLiteral("-nostdin") << QStringLiteral("-nostats")
                  << QStringLiteral("-progress") << QStringLiteral("pipe:1");
        // Input-side seeking uses the index instead of decoding the entire
        // prefix for EVERY cut. In Exact mode FFmpeg's default accurate_seek
        // discards frames between the preceding keyframe and the target.
        arguments << QStringLiteral("-ss") << seconds(request.startSeconds);
        const unsigned threads = std::clamp(std::thread::hardware_concurrency() / 2, 1u, 8u);
        arguments << QStringLiteral("-threads") << QString::number(threads);
        arguments << QStringLiteral("-i")
                  << QString::fromStdWString(request.inputPath.wstring());
        if (request.ensureStereoAudio && !request.sourceHasAudio)
            arguments << "-f" << "lavfi" << "-i" << "anullsrc=r=48000:cl=stereo";
        // This is a video-clip export, not a container clone. Mapping every
        // stream also selects subtitles/data/attachments whose codecs may not
        // be supported by MP4 (including the "codec none" encoder failure).
        // Require the primary video, retain optional audio, omit other streams.
        arguments << QStringLiteral("-t") << seconds(duration)
                  << QStringLiteral("-map") << QStringLiteral("0:v:0")
                  << QStringLiteral("-map") << (request.ensureStereoAudio
                    ? (request.sourceHasAudio ? QStringLiteral("0:a:0") : QStringLiteral("1:a:0"))
                    : QStringLiteral("0:a?"));
        if (request.mode == CutMode::Fast) {
            arguments << QStringLiteral("-c") << QStringLiteral("copy");
        } else {
            arguments << QStringLiteral("-c:v") << QString::fromStdString(encoder)
                      << QStringLiteral("-c:a") << QStringLiteral("aac")
                      << QStringLiteral("-movflags") << QStringLiteral("+faststart");
            arguments << "-b:a" << "256k";
            for (const auto& option : videoEncodingArguments(encoder, request.quality, request.videoBitrateKbps, threads))
                arguments << QString::fromStdString(option);
            if (montage) {
                arguments << "-vf" << QString("scale=%1:%2:force_original_aspect_ratio=decrease:force_divisible_by=2,pad=%1:%2:(ow-iw)/2:(oh-ih)/2,setsar=1,fps=%3,format=yuv420p")
                    .arg(request.canvasWidth).arg(request.canvasHeight).arg(request.outputFrameRate, 0, 'g', 12);
                arguments << "-profile:v" << "high" << "-ar" << "48000" << "-ac" << "2"
                          << "-af" << "aresample=async=1:first_pts=0,apad";
            } else if (request.maxWidth > 0 || request.maxHeight > 0) {
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
        qint64 lastAdvanceMs = 0;
        double encodedSeconds = -1.0;
        QByteArray pendingProgress;
        QByteArray diagnostic;
        bool stalled = false;
        const auto readProgress = [&] {
            diagnostic += process.readAllStandardError();
            // Retain actionable errors without growing memory for hours.
            if (diagnostic.size() > 65536) diagnostic = diagnostic.right(65536);
            pendingProgress += process.readAllStandardOutput();
            qsizetype newline;
            while ((newline = pendingProgress.indexOf('\n')) >= 0) {
                const auto line = pendingProgress.left(newline).trimmed();
                pendingProgress.remove(0, newline + 1);
                if (!line.startsWith("out_time_us=")) continue;
                bool ok = false;
                const double current = line.mid(12).toDouble(&ok) / 1000000.0;
                if (ok && current > encodedSeconds) {
                    encodedSeconds = current;
                    lastAdvanceMs = elapsed.elapsed();
                    if (request.progress) request.progress(std::clamp(current, 0.0, duration));
                }
            }
            if (pendingProgress.size() > 65536) pendingProgress.clear();
        };
        while (process.state() != QProcess::NotRunning
               && !request.stopToken.stop_requested()
               && (request.timeoutMs <= 0 || elapsed.elapsed() < request.timeoutMs)) {
            process.waitForFinished(100);
            readProgress();
            if (process.state() != QProcess::NotRunning && request.stallTimeoutMs > 0
                && elapsed.elapsed() - lastAdvanceMs >= request.stallTimeoutMs) {
                stalled = true;
                break;
            }
        }
        readProgress();
        if (request.stopToken.stop_requested() || process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(5000);
            result.cancelled = request.stopToken.stop_requested();
            result.error = result.cancelled ? "export cancelled"
                : std::string(stalled ? "ffmpeg stopped advancing: " : "ffmpeg timed out: ")
                    + diagnostic.toStdString() + processError(process);
            std::filesystem::remove(temporaryPath, filesystemError);
            return result;
        }
        result.exitCode = process.exitCode();
        if (process.exitStatus() == QProcess::NormalExit && result.exitCode == 0
            && std::filesystem::is_regular_file(temporaryPath, filesystemError)) {
            filesystemError.clear();
            installCut(temporaryPath, request.outputPath, filesystemError);
            if (!filesystemError) {
                result.success = true;
                result.encoder = encoder;
                result.error.clear();
                if (request.mode == CutMode::Exact) {
                    std::lock_guard lock(capabilitiesMutex_);
                    preferredEncoder_ = encoder;
                }
                if (request.progress) request.progress(duration);
                return result;
            }
            result.error = "move cut into place: " + filesystemError.message();
            break;
        }
        result.error += result.executable + " [" + encoder + ", exit "
            + std::to_string(result.exitCode) + "]: " + diagnostic.toStdString()
            + processError(process) + "\n";
        std::filesystem::remove(temporaryPath, filesystemError);
        if (request.mode == CutMode::Fast) break;
    }
    std::filesystem::remove(temporaryPath, filesystemError);
    if (result.error.empty()) result.error = "ffmpeg failed to create the cut";
    return result;
}

} // namespace pfservices
