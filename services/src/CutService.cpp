#include <pfservices/CutService.hpp>

#include <QFile>
#include <QProcess>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <system_error>
#include <utility>
#include <vector>

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
    const auto temporaryPath = request.outputPath.string() + ".part";
    std::filesystem::remove(temporaryPath, filesystemError);

    std::vector<std::string> encoders;
    if (request.mode == CutMode::Fast) encoders.emplace_back("copy");
    else encoders = {"h264_nvenc", "h264_amf", "h264_qsv", "libx264"};

    for (const auto& encoder : encoders) {
        QStringList arguments;
        arguments << QStringLiteral("-hide_banner") << QStringLiteral("-loglevel")
                  << QStringLiteral("error") << QStringLiteral("-y");
        if (request.mode == CutMode::Fast)
            arguments << QStringLiteral("-ss") << seconds(request.startSeconds);
        arguments << QStringLiteral("-i")
                  << QString::fromStdWString(request.inputPath.wstring());
        if (request.mode == CutMode::Exact)
            arguments << QStringLiteral("-ss") << seconds(request.startSeconds);
        arguments << QStringLiteral("-t") << seconds(duration)
                  << QStringLiteral("-map") << QStringLiteral("0");
        if (request.mode == CutMode::Fast) {
            arguments << QStringLiteral("-c") << QStringLiteral("copy");
        } else {
            arguments << QStringLiteral("-c:v") << QString::fromStdString(encoder)
                      << QStringLiteral("-c:a") << QStringLiteral("aac")
                      << QStringLiteral("-movflags") << QStringLiteral("+faststart");
            if (request.maxWidth > 0 || request.maxHeight > 0) {
                // min(iw/ih, limit) prevents upscaling while preserving aspect ratio.
                const int width = request.maxWidth > 0 ? request.maxWidth : 100000;
                const int height = request.maxHeight > 0 ? request.maxHeight : 100000;
                const QString filter = QStringLiteral("scale=min(iw\\,%1):min(ih\\,%2):force_original_aspect_ratio=decrease")
                    .arg(width).arg(height);
                arguments << QStringLiteral("-vf") << filter;
            }
        }
        arguments << QString::fromStdWString(std::filesystem::path(temporaryPath).wstring());

        QProcess process;
        process.setProgram(QString::fromStdString(ffmpegExecutable_));
        process.setArguments(arguments);
        process.start();
        if (!process.waitForStarted(5000)) {
            result.error = processError(process);
            return result;
        }
        if (!process.waitForFinished(-1)) {
            process.kill();
            process.waitForFinished(1000);
            result.error = "ffmpeg did not finish: " + processError(process);
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
                return result;
            }
            result.error = "move cut into place: " + filesystemError.message();
            break;
        }
        result.error = processError(process);
        std::filesystem::remove(temporaryPath, filesystemError);
        if (request.mode == CutMode::Fast) break;
    }
    std::filesystem::remove(temporaryPath, filesystemError);
    if (result.error.empty()) result.error = "ffmpeg failed to create the cut";
    return result;
}

} // namespace pfservices
