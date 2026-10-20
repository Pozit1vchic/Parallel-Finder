#include <pfservices/CutService.hpp>
#include <pfcore/VideoDecoder.hpp>

#include <QFile>
#include <QCoreApplication>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QScopeGuard>
#include <QStringList>
#include <QDir>
#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <system_error>
#include <utility>
#include <vector>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace pfservices {
bool isSupportedVideoContainer(std::string_view extension)
{
    if (!extension.empty() && extension.front() == '.') extension.remove_prefix(1);
    std::string normalized(extension);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return normalized == "mp4" || normalized == "mov" || normalized == "mkv" || normalized == "webm";
}

namespace {

QString seconds(double value)
{
    return QString::number(value, 'f', 6);
}

QImage transitionSignatureImage(const QString& text)
{
    // Keep this a small transparent overlay; FFmpeg scales it to the output
    // width, avoiding a full-resolution RGBA image per export worker.
    QImage image(1280, 160, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    QFont font(QStringLiteral("Segoe UI"));
    font.setPixelSize(42);
    font.setWeight(QFont::DemiBold);
    const int cardWidth = std::clamp(QFontMetrics(font).horizontalAdvance(text) + 112, 300, 1216);
    while (font.pixelSize() > 20 && QFontMetrics(font).horizontalAdvance(text) > cardWidth - 104)
        font.setPixelSize(font.pixelSize() - 2);

    const QRect card((image.width() - cardWidth) / 2, 18, cardWidth, 124);
    painter.setPen(QPen(QColor(255, 255, 255, 58), 1));
    painter.setBrush(QColor(12, 15, 22, 224));
    painter.drawRoundedRect(card, 22, 22);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(83, 143, 255));
    painter.drawRoundedRect(QRect(card.left() + 16, card.top() + 25, 5, card.height() - 50), 2, 2);
    painter.setFont(font);
    painter.setPen(QColor(255, 255, 255));
    painter.drawText(card.adjusted(42, 8, -28, -8), Qt::AlignCenter | Qt::TextSingleLine, text);
    return image;
}

QString signatureFilter(const QString& baseFilter, int imageInput)
{
    return QStringLiteral("[0:v]%1[pf_base];[").arg(baseFilter)
        + QString::number(imageInput)
        + QStringLiteral(":v]format=rgba[pf_sig];[pf_sig][pf_base]"
            "scale2ref=w='min(1600,main_w*0.84)':h=-1[pf_sig_scaled][pf_base_scaled];"
            "[pf_base_scaled][pf_sig_scaled]overlay=x=(W-w)/2:y=H*0.06:eof_action=repeat:format=auto[pf_video]");
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
    } else if (encoder == "libvpx-vp9") {
        args = {"-deadline", "good", "-cpu-used", "2", "-row-mt", "1",
            "-threads", std::to_string(threads)};
        if (bitrateKbps == 0) args.insert(args.end(), {"-crf", q, "-b:v", "0"});
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
    const bool lossless = request.mode == CutMode::ExactLossless;
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
    if (request.maxWidth > 32768 || request.maxHeight > 32768) {
        result.error = "maximum output dimensions exceed the supported limit";
        return result;
    }
    const QString outputExtension = QString::fromStdWString(request.outputPath.extension().wstring()).toLower();
    if (!isSupportedVideoContainer(outputExtension.toStdString())) {
        result.error = "unsupported video container; choose MP4, MOV, MKV, or WebM";
        return result;
    }
    if (request.mode == CutMode::Fast && (request.maxWidth > 0 || request.maxHeight > 0)) {
        result.error = "resolution cap requires exact cut mode";
        return result;
    }
    if (!request.transitionSignature.trimmed().isEmpty() && request.mode != CutMode::Exact) {
        result.error = "transition signatures require exact re-encoding; fast and lossless modes cannot burn in text";
        return result;
    }
    if (request.mode == CutMode::Fast && request.videoBitrateKbps > 0) {
        result.error = "target bitrate requires re-encoding; choose exact cut mode";
        return result;
    }
    const bool losslessMontage = lossless && !request.losslessPixelFormat.isEmpty();
    const bool montage = request.canvasWidth > 0 || request.canvasHeight > 0 || request.ensureStereoAudio;
    if(lossless && (outputExtension != ".mov"
        || request.maxWidth || request.maxHeight || (montage && !losslessMontage) || request.videoBitrateKbps)) {
        result.error="exact lossless requires MOV without resizing, normalization, or target bitrate";
        return result;
    }
    if (losslessMontage && (request.canvasWidth < 2 || request.canvasHeight < 2
        || request.canvasWidth > 32768 || request.canvasHeight > 32768
        || static_cast<int64_t>(request.canvasWidth)*request.canvasHeight > 64LL*1024*1024
        || request.losslessAudioRate < 0 || request.losslessAudioRate > 768000
        || request.losslessAudioChannels < 0 || request.losslessAudioChannels > 64)) {
        result.error = "invalid lossless montage plan"; return result;
    }
    if (montage && !losslessMontage && (request.mode != CutMode::Exact || request.canvasWidth < 2 || request.canvasHeight < 2
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

    std::optional<pfcore::VideoInfo> sourceInfo;
    if (lossless) {
        try { pfcore::VideoDecoder decoder; decoder.open(QString::fromStdWString(request.inputPath.wstring()).toUtf8().toStdString()); sourceInfo = decoder.info(); }
        catch (const std::exception& error) { result.error = error.what(); return result; }
    }
    const double duration = request.endSeconds - request.startSeconds;
    QString signaturePath;
    QTemporaryDir signatureDirectory;
    if (!request.transitionSignature.trimmed().isEmpty()) {
        if (!signatureDirectory.isValid()) {
            result.error = "cannot create temporary transition-signature image";
            return result;
        }
        signaturePath = signatureDirectory.filePath(QStringLiteral("transition.png"));
        if (!transitionSignatureImage(request.transitionSignature.trimmed()).save(signaturePath, "PNG")) {
            result.error = "cannot render transition-signature image";
            return result;
        }
    }
    const QString ffmpegProgram = resolveFfmpegExecutable(ffmpegExecutable_);
    result.executable = ffmpegProgram.toStdString();
    // Keep the real media extension on the temporary file.  FFmpeg selects
    // its muxer from the output suffix; `clip.mp4.part` has no known format
    // and produces "Unable to choose an output format" on Windows.
    const std::string extension = request.outputPath.extension().string();
    std::filesystem::path temporaryPath;
    {
        QTemporaryFile temporaryFile(QFileInfo(QString::fromStdWString(request.outputPath.wstring())).absolutePath() + "/"
            + QString::fromStdWString(request.outputPath.stem().wstring()) + ".part-XXXXXX"
            + QString::fromStdString(extension.empty() ? std::string(".mp4") : extension));
        if (!temporaryFile.open()) { result.error = temporaryFile.errorString().toStdString(); return result; }
        temporaryPath = std::filesystem::path(temporaryFile.fileName().toStdWString());
        temporaryFile.setAutoRemove(false);
    } // Release the native Windows handle before FFmpeg/rename access it.
    const auto temporaryCleanup = qScopeGuard([temporaryPath] {
        std::error_code error; std::filesystem::remove(temporaryPath, error);
    });

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
        const bool webm = outputExtension == ".webm";
        if (webm) {
            if (std::find(capabilities_->videoEncoders.begin(), capabilities_->videoEncoders.end(), "libvpx-vp9")
                != capabilities_->videoEncoders.end()) encoders.emplace_back("libvpx-vp9");
            if (std::none_of(capabilities_->audioEncoders.begin(), capabilities_->audioEncoders.end(), [](const auto& name) {
                return name == "libopus" || name == "libvorbis";
            })) {
                result.error = result.executable + ": WebM export requires libopus or libvorbis";
                return result;
            }
        } else encoders = capabilities_->h264Candidates();
        if(lossless) {
            encoders.clear();
            const bool commonH264 = losslessMontage && (request.losslessPixelFormat == "yuv420p"
                || request.losslessPixelFormat == "yuv422p" || request.losslessPixelFormat == "yuv444p"
                || request.losslessPixelFormat == "yuv420p10le" || request.losslessPixelFormat == "yuv422p10le"
                || request.losslessPixelFormat == "yuv444p10le" || request.losslessPixelFormat == "yuvj420p"
                || request.losslessPixelFormat == "yuvj422p" || request.losslessPixelFormat == "yuvj444p");
            const std::string commonEncoder = commonH264 ? "libx264" : "ffv1";
            for(const auto* name:{"hevc_nvenc", "libx264", "ffv1"})
                if(std::find(capabilities_->videoEncoders.begin(),capabilities_->videoEncoders.end(),name)
                    !=capabilities_->videoEncoders.end() && (!losslessMontage || std::string(name)==commonEncoder))encoders.emplace_back(name);
            if(std::find(capabilities_->audioEncoders.begin(),capabilities_->audioEncoders.end(),"pcm_f64le")
                ==capabilities_->audioEncoders.end()) {
                result.error="exact lossless requires the pcm_f64le audio encoder";
                return result;
            }
        }
        // Compiled-in hardware support does not mean this PC has that GPU.
        // Try the encoder that actually succeeded first for subsequent clips.
        const auto preferred = std::find(encoders.begin(), encoders.end(), preferredEncoder_);
        if (!losslessMontage && preferred != encoders.end()) std::rotate(encoders.begin(), preferred, preferred + 1);
        if (!capabilities_->error.empty() || encoders.empty()) {
            result.error = result.executable + ": " + (capabilities_->error.empty()
                ? (webm ? "no supported VP9 encoder; install an FFmpeg build with libvpx-vp9"
                        : "no supported H.264 encoder; install an FFmpeg build with libx264")
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
        const bool timestampTrim = request.mode != CutMode::Fast;
        // Preserve the source clock until trimming. Resetting it at a seek
        // between frames rounds the end against the wrong frame origin.
        // A bounded preroll also retains partial audio packets at the start.
        const double seekStart = timestampTrim ? std::max(0.0, request.startSeconds - 0.5)
                                                : request.startSeconds;
        if (timestampTrim) arguments << "-copyts";
        arguments << QStringLiteral("-ss") << seconds(seekStart);
        if (timestampTrim)
            arguments << "-t" << seconds(request.endSeconds - seekStart + 0.25);
        const unsigned threads = request.workerThreads ? std::clamp(request.workerThreads, 1u, 8u)
            : std::clamp(std::thread::hardware_concurrency() / 2, 1u, 8u);
        arguments << "-filter_threads" << QString::number(threads);
        arguments << QStringLiteral("-threads") << QString::number(threads);
        arguments << QStringLiteral("-i")
                  << QString::fromStdWString(request.inputPath.wstring());
        const bool silence = losslessMontage && request.losslessAudioRate > 0 && !request.sourceHasAudio;
        if (silence) arguments << "-f" << "lavfi" << "-i"
            << QString("anullsrc=r=%1:cl=%2c").arg(request.losslessAudioRate).arg(request.losslessAudioChannels);
        if (request.ensureStereoAudio && !request.sourceHasAudio)
            arguments << "-f" << "lavfi" << "-i" << "anullsrc=r=48000:cl=stereo";
        const int signatureInput = request.ensureStereoAudio && !request.sourceHasAudio ? 2 : 1;
        if (!signaturePath.isEmpty()) arguments << "-i" << signaturePath;
        // This is a video-clip export, not a container clone. Mapping every
        // stream also selects subtitles/data/attachments whose codecs may not
        // be supported by MP4 (including the "codec none" encoder failure).
        // Require the primary video, retain optional audio, omit other streams.
        arguments << QStringLiteral("-t") << seconds(duration)
                  << QStringLiteral("-map") << (signaturePath.isEmpty()
                    ? QStringLiteral("0:v:0") : QStringLiteral("[pf_video]"))
                  << QStringLiteral("-map") << (silence ? QStringLiteral("1:a:0") : request.ensureStereoAudio
                    ? (request.sourceHasAudio ? QStringLiteral("0:a:0") : QStringLiteral("1:a:0"))
                    : losslessMontage ? QStringLiteral("0:a:0?") : QStringLiteral("0:a?"));
        if (request.mode == CutMode::Fast) {
            arguments << QStringLiteral("-c") << QStringLiteral("copy");
            // Keep the preceding keyframe and its dependent frames visible.
            // Negative preroll/edit-list timestamps otherwise overlap when
            // independently sought clips are concatenated (especially HEVC).
            arguments << "-avoid_negative_ts" << "make_zero";
        } else {
            const bool webm = outputExtension == ".webm";
            const QString audioEncoder = lossless ? QStringLiteral("pcm_f64le")
                : webm
                    ? (std::find(capabilities_->audioEncoders.begin(), capabilities_->audioEncoders.end(), "libopus")
                        != capabilities_->audioEncoders.end() ? QStringLiteral("libopus") : QStringLiteral("libvorbis"))
                    : QStringLiteral("aac");
            arguments << QStringLiteral("-c:v") << QString::fromStdString(encoder)
                      << QStringLiteral("-c:a") << audioEncoder;
            if (outputExtension == ".mp4" || outputExtension == ".mov")
                arguments << QStringLiteral("-movflags") << QStringLiteral("+faststart");
            if(lossless) {
                if (sourceInfo) {
                    if (sourceInfo->colorRange) arguments << "-color_range" << QString::number(sourceInfo->colorRange);
                    if (sourceInfo->colorPrimaries != 2) arguments << "-color_primaries" << QString::number(sourceInfo->colorPrimaries);
                    if (sourceInfo->colorTransfer != 2) arguments << "-color_trc" << QString::number(sourceInfo->colorTransfer);
                    if (sourceInfo->colorSpace != 2) arguments << "-colorspace" << QString::number(sourceInfo->colorSpace);
                }
                arguments << "-movflags" << "+faststart+write_colr";
                // '+' preserves the input pixel format and rejects automatic
                // conversions when an encoder cannot represent that format.
                arguments << "-pix_fmt" << "+";
                if(encoder=="ffv1") arguments << "-level" << "3" << "-coder" << "1" << "-context" << "1" << "-threads:v" << QString::number(threads);
                else if(encoder=="libx264")arguments << "-preset" << "veryfast" << "-qp" << "0" << "-threads:v" << QString::number(threads);
                else arguments << "-preset" << "p5" << "-tune" << "lossless";
            } else {
                arguments << "-b:a" << (webm ? QStringLiteral("160k") : QStringLiteral("256k"));
                for (const auto& option : videoEncodingArguments(encoder, request.quality, request.videoBitrateKbps, threads))
                    arguments << QString::fromStdString(option);
            }
            if (losslessMontage) {
                QString filter = QString("trim=start=%1:end=%2,setpts=PTS-%1/TB")
                    .arg(seconds(request.startSeconds), seconds(request.endSeconds));
                // Keep native samples for equal formats. Mixed YUV sources expand
                // chroma with nearest-neighbour sampling, never downsample to 420.
                filter += QString(",scale=iw:ih:flags=neighbor+bitexact,format=%1")
                    .arg(request.losslessPixelFormat);
                filter += QString(",scale=%1:%2:flags=neighbor+bitexact,pad=%3:%4:floor((ow-iw)/2):floor((oh-ih)/2),setsar=1")
                    .arg(request.losslessDisplayWidth).arg(request.losslessDisplayHeight)
                    .arg(request.canvasWidth).arg(request.canvasHeight);
                arguments << "-vf" << filter << "-fps_mode" << "passthrough"
                          << "-enc_time_base:v" << "1:1000000" << "-video_track_timescale" << "1000000";
                if (request.losslessAudioRate > 0) {
                    const QString trim = silence
                        ? QString("atrim=end=%1,asetpts=PTS-STARTPTS").arg(seconds(duration))
                        : QString("atrim=start=%1:end=%2,asetpts=PTS-%1/TB")
                            .arg(seconds(request.startSeconds),seconds(request.endSeconds));
                    // Matching rates pass samples through unchanged; only mixed
                    // rates/layouts require resampling. Never use AAC in lossless.
                    arguments << "-ar" << QString::number(request.losslessAudioRate)
                              << "-ac" << QString::number(request.losslessAudioChannels)
                              << "-af" << trim + ",apad";
                }
            } else if (montage) {
                const QString trim = QString("trim=start=%1:end=%2,setpts=PTS-%1/TB,")
                    .arg(seconds(request.startSeconds), seconds(request.endSeconds));
                const QString videoFilter = trim + QString("scale=w='min(iw\\,%1)':h='min(ih\\,%2)':force_original_aspect_ratio=decrease:force_divisible_by=2,pad=%1:%2:(ow-iw)/2:(oh-ih)/2,setsar=1,fps=%3:round=down,format=yuv420p")
                    .arg(request.canvasWidth).arg(request.canvasHeight).arg(request.outputFrameRate, 0, 'g', 12);
                if (signaturePath.isEmpty()) arguments << "-vf" << videoFilter;
                else arguments << "-filter_complex" << signatureFilter(videoFilter, signatureInput);
                const QString audioTrim = request.ensureStereoAudio && !request.sourceHasAudio
                    ? QString("atrim=end=%1,asetpts=PTS-STARTPTS,").arg(seconds(duration))
                    : QString("atrim=start=%1:end=%2,asetpts=PTS-%1/TB,")
                        .arg(seconds(request.startSeconds), seconds(request.endSeconds));
                if (!webm) arguments << "-profile:v" << "high";
                arguments << "-ar" << "48000" << "-ac" << "2"
                          << "-af" << audioTrim + "aresample=async=1:first_pts=0,apad";
            } else {
                QString filter = QString("trim=start=%1:end=%2,setpts=PTS-%1/TB")
                    .arg(seconds(request.startSeconds), seconds(request.endSeconds));
                arguments << "-af" << QString("atrim=start=%1:end=%2,asetpts=PTS-%1/TB")
                    .arg(seconds(request.startSeconds), seconds(request.endSeconds));
                arguments << "-fps_mode" << "passthrough";
                // The nominal frame-rate timebase can round a sub-frame
                // offset up and push the final frame onto the exclusive end.
                arguments << "-enc_time_base:v" << "demux";
                if (request.maxWidth > 0 || request.maxHeight > 0) {
                    // min(iw/ih, limit) prevents upscaling while preserving aspect ratio.
                    const int width = request.maxWidth > 0 ? request.maxWidth : 32768;
                    const int height = request.maxHeight > 0 ? request.maxHeight : 32768;
                    filter += QStringLiteral(",scale=min(iw\\,%1):min(ih\\,%2):force_original_aspect_ratio=decrease:force_divisible_by=2")
                        .arg(width).arg(height);
                }
                if (signaturePath.isEmpty()) arguments << QStringLiteral("-vf") << filter;
                else arguments << "-filter_complex" << signatureFilter(filter, signatureInput);
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
                if (request.mode != CutMode::Fast) {
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
