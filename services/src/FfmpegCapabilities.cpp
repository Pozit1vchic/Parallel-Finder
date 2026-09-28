#include <pfservices/FfmpegCapabilities.hpp>
#include <QProcess>
#include <QStringList>
#include <QElapsedTimer>
#include <algorithm>
#include <sstream>
#if defined(_WIN32)
#include <windows.h>
#endif

namespace pfservices {
FfmpegCapabilities FfmpegCapabilities::parse(std::string_view output)
{
    FfmpegCapabilities result;
    std::istringstream lines{std::string(output)};
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream fields(line);
        std::string flags, name;
        if (!(fields >> flags >> name) || flags.size() != 6 || name == "=") continue;
        auto* encoders = flags[0] == 'V' ? &result.videoEncoders
            : flags[0] == 'A' ? &result.audioEncoders : nullptr;
        if (encoders && std::find(encoders->begin(), encoders->end(), name) == encoders->end())
            encoders->push_back(name);
    }
    return result;
}

FfmpegCapabilities FfmpegCapabilities::probe(const std::string& executable, std::stop_token stop)
{
    QProcess process;
#if defined(_WIN32)
    process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW;
    });
#endif
    process.start(QString::fromStdString(executable), {"-hide_banner", "-encoders"});
    FfmpegCapabilities result;
    if (!process.waitForStarted(5000)) {
        result.error = process.errorString().toStdString();
    } else {
        QElapsedTimer elapsed;
        elapsed.start();
        while (process.state() != QProcess::NotRunning && !stop.stop_requested() && elapsed.elapsed() < 10000)
            process.waitForFinished(100);
        if (stop.stop_requested() || process.state() != QProcess::NotRunning) {
            process.kill();
            process.waitForFinished(5000);
            result.error = stop.stop_requested() ? "encoder probe cancelled" : "encoder probe timed out";
        } else if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            result.error = process.readAllStandardError().toStdString();
            if (result.error.empty()) result.error = "encoder probe failed";
        } else {
            result = parse(process.readAllStandardOutput().toStdString());
            if (result.videoEncoders.empty()) result.error = "FFmpeg reported no video encoders";
        }
    }
    return result;
}

std::vector<std::string> FfmpegCapabilities::h264Candidates() const
{
    std::vector<std::string> result;
    for (const auto* name : {"h264_nvenc", "h264_amf", "h264_qsv", "libx264"})
        if (std::find(videoEncoders.begin(), videoEncoders.end(), name) != videoEncoders.end())
            result.emplace_back(name);
    return result;
}
}
