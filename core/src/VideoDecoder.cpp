#include "pfcore/VideoDecoder.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
#include <chrono>
#include <thread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/display.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
#include <libavutil/hwcontext.h>
#include <libswscale/swscale.h>
}

namespace pfcore {
namespace {

constexpr std::uint64_t kMaxDecodedFramePixels = 64ULL * 1024ULL * 1024ULL;

std::string ffError(int code)
{
    char buffer[AV_ERROR_MAX_STRING_SIZE] {};
    av_strerror(code, buffer, sizeof(buffer));
    return buffer;
}

double rationalOr(const AVRational value, double fallback)
{
    return value.den ? av_q2d(value) : fallback;
}

std::int64_t seekTimestamp(double seconds, AVRational timeBase)
{
    if (!std::isfinite(seconds))
        throw std::invalid_argument("seek timestamp must be finite");
    if (timeBase.num <= 0 || timeBase.den <= 0)
        throw std::runtime_error("video has an invalid seek time base");
    // Retain the existing division/rounding for valid requests. Use the exact
    // exclusive bound 2^63: double(INT64_MAX) rounds up to that invalid value.
    const double ticks = std::max(0.0, seconds) / av_q2d(timeBase);
    const double upperBound = std::ldexp(1.0, std::numeric_limits<std::int64_t>::digits);
    if (!std::isfinite(ticks)
        || ticks >= upperBound)
        throw std::invalid_argument("seek timestamp exceeds the supported range");
    return static_cast<std::int64_t>(ticks);
}

struct ElapsedMeasurement {
    double& total;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    ~ElapsedMeasurement() {
        total += std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
    }
};

const char* cuvidDecoder(AVCodecID codec)
{
    switch (codec) {
    case AV_CODEC_ID_H264: return "h264_cuvid";
    case AV_CODEC_ID_HEVC: return "hevc_cuvid";
    case AV_CODEC_ID_AV1: return "av1_cuvid";
    case AV_CODEC_ID_VP9: return "vp9_cuvid";
    default: return nullptr;
    }
}

} // namespace

struct VideoDecoder::Impl {
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* decoded = nullptr;
    AVFrame* downloaded = nullptr;
    std::vector<std::uint8_t> chromaU, chromaV;
    SwsContext* scaler = nullptr;
    int scalerSourceWidth = 0;
    int scalerSourceHeight = 0;
    int scalerTargetWidth = 0;
    int scalerTargetHeight = 0;
    AVPixelFormat scalerFormat = AV_PIX_FMT_NONE;
    int streamIndex = -1;
    VideoInfo metadata;
    bool draining = false;
    bool frameAvailable = false;
    int rgbaMaxWidth = 0;
    int rgbaMaxHeight = 0;
    VideoDecodeOptions options;
    VideoDecodeDiagnostics diagnostics;
    bool hardware = false;
    double lastDelivered = -std::numeric_limits<double>::infinity();
    double recoveryTarget = -std::numeric_limits<double>::infinity();
    bool recoveryIncludeTarget = false;

    ~Impl() { reset(); }

    void reset() noexcept
    {
        if (scaler) sws_freeContext(scaler);
        if (decoded) av_frame_free(&decoded);
        if (downloaded) av_frame_free(&downloaded);
        if (packet) av_packet_free(&packet);
        if (codec) avcodec_free_context(&codec);
        if (format) avformat_close_input(&format);
        scaler = nullptr;
        scalerSourceWidth = 0;
        scalerSourceHeight = 0;
        scalerTargetWidth = 0;
        scalerTargetHeight = 0;
        scalerFormat = AV_PIX_FMT_NONE;
        streamIndex = -1;
        draining = false;
        frameAvailable = false;
        rgbaMaxWidth = 0;
        rgbaMaxHeight = 0;
        metadata = {};
        options = {};
        diagnostics = {};
        hardware = false;
        std::vector<std::uint8_t>().swap(chromaU);
        std::vector<std::uint8_t>().swap(chromaV);
        lastDelivered = recoveryTarget = -std::numeric_limits<double>::infinity();
        recoveryIncludeTarget = false;
    }

    int openCodec(const AVCodec* decoder, bool useHardware, int width = 0, int height = 0)
    {
        avcodec_free_context(&codec);
        hardware = false;
        codec = avcodec_alloc_context3(decoder);
        if (!codec) return AVERROR(ENOMEM);
        int result = avcodec_parameters_to_context(codec, format->streams[streamIndex]->codecpar);
        if (result < 0) return result;
        codec->pkt_timebase = format->streams[streamIndex]->time_base;
        codec->thread_count = useHardware ? 1 : diagnostics.threads;
        codec->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
        AVDictionary* codecOptions = nullptr;
        if (useHardware) {
            const auto device = std::to_string(options.nvidiaDevice);
            result = av_hwdevice_ctx_create(&codec->hw_device_ctx, AV_HWDEVICE_TYPE_CUDA,
                                            device.c_str(), nullptr, 0);
            if (result < 0) return result;
            codec->get_format = [](AVCodecContext*, const AVPixelFormat* formats) {
                for (const auto* format = formats; *format != AV_PIX_FMT_NONE; ++format)
                    if (*format == AV_PIX_FMT_CUDA) return AV_PIX_FMT_CUDA;
                return AV_PIX_FMT_NONE;
            };
            if (options.resizeOnNvidia) {
                const auto resize = std::to_string(width) + "x" + std::to_string(height);
                av_dict_set(&codecOptions, "resize", resize.c_str(), 0);
            }
        }
        result = avcodec_open2(codec, decoder, &codecOptions);
        av_dict_free(&codecOptions);
        if (result >= 0) {
            hardware = useHardware;
            diagnostics.backend = hardware ? "nvdec" : "cpu";
        }
        return result;
    }

    void fallBackToCpu(const std::string& reason, double target, bool includeTarget)
    {
        diagnostics.fallbackReason = reason;
        const auto* stream = format->streams[streamIndex];
        const double seekSeconds = std::isfinite(target) ? std::max(0.0, target) : 0.0;
        const auto timestamp = seekTimestamp(seekSeconds, stream->time_base);
        const auto* software = avcodec_find_decoder(stream->codecpar->codec_id);
        if (!software) throw std::runtime_error("no software decoder for hardware fallback");
        av_frame_unref(decoded);
        if (downloaded) av_frame_unref(downloaded);
        frameAvailable = false;
        av_packet_unref(packet);
        const int result = openCodec(software, false);
        if (result < 0) throw std::runtime_error("open fallback decoder: " + ffError(result));
        const int seekResult = av_seek_frame(format, streamIndex, timestamp, AVSEEK_FLAG_BACKWARD);
        if (seekResult < 0) throw std::runtime_error("seek fallback decoder: " + ffError(seekResult));
        draining = false;
        recoveryTarget = target;
        recoveryIncludeTarget = includeTarget;
    }

    void copyCurrentFrameToRgba(DecodedFrame& output)
    {
        ElapsedMeasurement measurement{diagnostics.conversionMilliseconds};
        const AVFrame* source = decoded;
        AVFrame planarView {};
        if (source->format == AV_PIX_FMT_CUDA) {
            if (!downloaded) downloaded = av_frame_alloc();
            if (!downloaded) throw std::runtime_error("allocate hardware download frame failed");
            av_frame_unref(downloaded);
            const int transfer = av_hwframe_transfer_data(downloaded, decoded, 0);
            if (transfer < 0) throw std::runtime_error("download GPU frame: " + ffError(transfer));
            const int props = av_frame_copy_props(downloaded, decoded);
            if (props < 0) throw std::runtime_error("copy hardware frame metadata: " + ffError(props));
            source = downloaded;
            ++diagnostics.hardwareDownloads;
        }
        if (decoded->format == AV_PIX_FMT_CUDA && source->format == AV_PIX_FMT_NV12) {
            // NV12 and planar YUV420 can take different swscale fast paths
            // (different rounding near scene/matcher thresholds). Deinterleave
            // chroma, borrowing the unchanged luma, to keep CPU analysis pixels.
            const int chromaWidth = (source->width + 1) / 2;
            const int chromaHeight = (source->height + 1) / 2;
            const auto size = static_cast<std::size_t>(chromaWidth) * chromaHeight;
            chromaU.resize(size);
            chromaV.resize(size);
            for (int y = 0; y < chromaHeight; ++y) {
                const auto* uv = source->data[1] + y * source->linesize[1];
                auto* u = chromaU.data() + static_cast<std::size_t>(y) * chromaWidth;
                auto* v = chromaV.data() + static_cast<std::size_t>(y) * chromaWidth;
                for (int x = 0; x < chromaWidth; ++x) {
                    u[x] = uv[2 * x];
                    v[x] = uv[2 * x + 1];
                }
            }
            planarView.width = source->width;
            planarView.height = source->height;
            planarView.format = AV_PIX_FMT_YUV420P;
            planarView.data[0] = source->data[0];
            planarView.linesize[0] = source->linesize[0];
            planarView.data[1] = chromaU.data();
            planarView.data[2] = chromaV.data();
            planarView.linesize[1] = planarView.linesize[2] = chromaWidth;
            source = &planarView; // borrowed pointers are used only in this call
        }
        const auto pixelFormat = static_cast<AVPixelFormat>(source->format);

        // Convert into display geometry, not merely coded pixel geometry.
        // Pose models must see anamorphic pixels corrected before inference.
        const double sar = metadata.sampleAspectRatio > 0.0
            ? std::clamp(metadata.sampleAspectRatio, 0.1, 10.0) : 1.0;
        const double displayWidth = std::max(1.0, source->width * sar);
        const double displayHeight = std::max(1.0, static_cast<double>(source->height));
        const double normalizedRotation = std::fmod(metadata.rotationDegrees + 360.0, 360.0);
        int quarterTurns = static_cast<int>(std::lround(normalizedRotation / 90.0)) % 4;
        const double snappedRotation = quarterTurns * 90.0;
        if (std::abs(normalizedRotation - snappedRotation) > 1.0
            && std::abs(normalizedRotation - (snappedRotation + 360.0)) > 1.0)
            quarterTurns = 0; // arbitrary rotation is uncommon; avoid cropping it silently
        const bool swapAxes = quarterTurns == 1 || quarterTurns == 3;
        const double finalWidth = swapAxes ? displayHeight : displayWidth;
        const double finalHeight = swapAxes ? displayWidth : displayHeight;
        const double scale = std::min({1.0,
            rgbaMaxWidth > 0 ? static_cast<double>(rgbaMaxWidth) / finalWidth : 1.0,
            rgbaMaxHeight > 0 ? static_cast<double>(rgbaMaxHeight) / finalHeight : 1.0});
        const double targetWidthDouble = displayWidth * scale;
        const double targetHeightDouble = displayHeight * scale;
        if (targetWidthDouble > static_cast<double>(std::numeric_limits<int>::max())
            || targetHeightDouble > static_cast<double>(std::numeric_limits<int>::max()))
            throw std::runtime_error("display-corrected frame dimensions overflow");
        const int targetWidth = std::max(1, static_cast<int>(std::lround(targetWidthDouble)));
        const int targetHeight = std::max(1, static_cast<int>(std::lround(targetHeightDouble)));
        if (static_cast<std::uint64_t>(targetWidth) * static_cast<std::uint64_t>(targetHeight)
            > kMaxDecodedFramePixels)
            throw std::runtime_error("display-corrected frame dimensions exceed the safe limit");
        if (!scaler || scalerSourceWidth != source->width
            || scalerSourceHeight != source->height
            || scalerTargetWidth != targetWidth || scalerTargetHeight != targetHeight
            || scalerFormat != pixelFormat) {
            if (scaler) sws_freeContext(scaler);
            scaler = sws_getContext(source->width, source->height,
                static_cast<AVPixelFormat>(source->format), targetWidth,
                targetHeight, AV_PIX_FMT_RGBA, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
            if (!scaler) throw std::runtime_error("create pixel converter failed");
            scalerSourceWidth = source->width;
            scalerSourceHeight = source->height;
            scalerTargetWidth = targetWidth;
            scalerTargetHeight = targetHeight;
            scalerFormat = pixelFormat;
        }
        output.width = targetWidth;
        output.height = targetHeight;
        const auto pixels = static_cast<std::size_t>(targetWidth)
            * static_cast<std::size_t>(targetHeight);
        if (pixels > std::numeric_limits<std::size_t>::max() / 4U)
            throw std::runtime_error("decoded frame buffer size overflow");
        output.rgba.resize(pixels * 4U);
        std::uint8_t* destination[] = { output.rgba.data() };
        int stride[] = { output.width * 4 };
        if (sws_scale(scaler, source->data, source->linesize, 0,
                      source->height, destination, stride) <= 0) {
            throw std::runtime_error("convert decoded frame to RGBA failed");
        }

        if (quarterTurns != 0) {
            const int sourceWidth = output.width;
            const int sourceHeight = output.height;
            const int rotatedWidth = swapAxes ? sourceHeight : sourceWidth;
            const int rotatedHeight = swapAxes ? sourceWidth : sourceHeight;
            std::vector<std::uint8_t> rotated(output.rgba.size());
            for (int y = 0; y < sourceHeight; ++y) {
                for (int x = 0; x < sourceWidth; ++x) {
                    int dx = x;
                    int dy = y;
                    if (quarterTurns == 1) { // 90 degrees clockwise
                        dx = sourceHeight - 1 - y;
                        dy = x;
                    } else if (quarterTurns == 2) {
                        dx = sourceWidth - 1 - x;
                        dy = sourceHeight - 1 - y;
                    } else if (quarterTurns == 3) { // 270 degrees clockwise
                        dx = y;
                        dy = sourceWidth - 1 - x;
                    }
                    const std::size_t src = (static_cast<std::size_t>(y) * sourceWidth + x) * 4U;
                    const std::size_t dst = (static_cast<std::size_t>(dy) * rotatedWidth + dx) * 4U;
                    std::copy_n(output.rgba.data() + src, 4, rotated.data() + dst);
                }
            }
            output.width = rotatedWidth;
            output.height = rotatedHeight;
            output.rgba.swap(rotated);
        }
        ++diagnostics.convertedFrames;
    }
};

VideoDecoder::VideoDecoder() : impl_(std::make_unique<Impl>()) {}
VideoDecoder::~VideoDecoder() = default;
VideoDecoder::VideoDecoder(VideoDecoder&&) noexcept = default;
VideoDecoder& VideoDecoder::operator=(VideoDecoder&&) noexcept = default;

void VideoDecoder::open(const std::string& path, VideoDecodeOptions options)
{
    close();
    if (options.threads < 0 || options.threads > 256 || options.nvidiaDevice < 0
        || options.maxWidth < 0 || options.maxHeight < 0)
        throw std::invalid_argument("invalid video decoder options");
    impl_->options = options;
    impl_->diagnostics.threads = options.threads > 0 ? std::min(options.threads, 8)
        : static_cast<int>(std::clamp(std::thread::hardware_concurrency() / 2, 1u, 8u));
    impl_->rgbaMaxWidth = options.maxWidth;
    impl_->rgbaMaxHeight = options.maxHeight;
    AVFormatContext* format = nullptr;
    int result = avformat_open_input(&format, path.c_str(), nullptr, nullptr);
    if (result < 0) throw std::runtime_error("open video '" + path + "': " + ffError(result));
    impl_->format = format;
    result = avformat_find_stream_info(format, nullptr);
    if (result < 0) { close(); throw std::runtime_error("read stream info: " + ffError(result)); }

    const AVStream* stream = nullptr;
    for (unsigned i = 0; i < format->nb_streams; ++i) {
        if (format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            stream = format->streams[i]; impl_->streamIndex = static_cast<int>(i); break;
        }
    }
    if (!stream) { close(); throw std::runtime_error("video contains no video stream"); }
    const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder) { close(); throw std::runtime_error("no decoder for video codec"); }
    impl_->codec = avcodec_alloc_context3(decoder);
    if (!impl_->codec) { close(); throw std::runtime_error("allocate decoder context failed"); }
    if ((result = avcodec_parameters_to_context(impl_->codec, stream->codecpar)) < 0) {
        close(); throw std::runtime_error("copy decoder parameters: " + ffError(result));
    }
    if (impl_->codec->width <= 0 || impl_->codec->height <= 0
        || static_cast<std::uint64_t>(impl_->codec->width)
            * static_cast<std::uint64_t>(impl_->codec->height) > kMaxDecodedFramePixels) {
        close();
        throw std::runtime_error("video frame dimensions exceed the safe decode limit");
    }
    impl_->packet = av_packet_alloc();
    impl_->decoded = av_frame_alloc();
    if (!impl_->packet || !impl_->decoded) { close(); throw std::runtime_error("allocate decode buffers failed"); }

    impl_->metadata.width = impl_->codec->width;
    impl_->metadata.hasAudio = av_find_best_stream(impl_->format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0) >= 0;
    impl_->metadata.height = impl_->codec->height;
    impl_->metadata.durationSeconds = stream->duration == AV_NOPTS_VALUE
        ? (format->duration == AV_NOPTS_VALUE ? 0.0 : format->duration / static_cast<double>(AV_TIME_BASE))
        : stream->duration * av_q2d(stream->time_base);
    if (impl_->metadata.durationSeconds <= 0.0 && format->duration > 0)
        impl_->metadata.durationSeconds = format->duration / static_cast<double>(AV_TIME_BASE);
    const AVRational rate = stream->avg_frame_rate.num ? stream->avg_frame_rate : stream->r_frame_rate;
    impl_->metadata.frameRate = rationalOr(rate, 0.0);
    if (impl_->metadata.durationSeconds <= 0.0 && stream->nb_frames > 0
        && impl_->metadata.frameRate > 0.0) {
        impl_->metadata.durationSeconds = static_cast<double>(stream->nb_frames)
            / impl_->metadata.frameRate;
    }
    impl_->metadata.variableFrameRate = stream->avg_frame_rate.num == 0
        || stream->avg_frame_rate.den == 0 || stream->r_frame_rate.num != stream->avg_frame_rate.num
        || stream->r_frame_rate.den != stream->avg_frame_rate.den;
    impl_->metadata.sampleAspectRatio = rationalOr(stream->sample_aspect_ratio, 1.0);
    if (impl_->metadata.sampleAspectRatio <= 0.0) impl_->metadata.sampleAspectRatio = 1.0;
    // Prefer the standards-based DISPLAYMATRIX side data.  The old `rotate`
    // metadata tag is retained as a fallback for legacy containers.
    const AVPacketSideData* displaySideData = av_packet_side_data_get(
        stream->codecpar->coded_side_data, stream->codecpar->nb_coded_side_data,
        AV_PKT_DATA_DISPLAYMATRIX);
    if (displaySideData && displaySideData->size >= 9 * static_cast<int>(sizeof(std::int32_t))) {
        const std::uint8_t* displayMatrix = displaySideData->data;
        const double rotation = -av_display_rotation_get(
            reinterpret_cast<const std::int32_t*>(displayMatrix));
        if (std::isfinite(rotation)) impl_->metadata.rotationDegrees = rotation;
    } else if (const AVDictionaryEntry* rotate = av_dict_get(stream->metadata, "rotate", nullptr, 0)) {
        try { impl_->metadata.rotationDegrees = std::stod(rotate->value); }
        catch (const std::exception&) { /* malformed metadata: keep zero */ }
    }
    // Keep source metadata intact: export/preview still refer to the original.
    // Rotated/anamorphic and unvalidated color formats retain the CPU path.
    bool openedHardware = false;
    if (options.preferNvidia) {
        const char* name = cuvidDecoder(stream->codecpar->codec_id);
        const auto* hardwareDecoder = name ? avcodec_find_decoder_by_name(name) : nullptr;
        if (!hardwareDecoder) impl_->diagnostics.fallbackReason = "codec has no CUVID decoder";
        else if (static_cast<std::uint64_t>(impl_->metadata.width) * impl_->metadata.height
                 < options.minimumNvidiaPixels)
            impl_->diagnostics.fallbackReason = "source below hardware size threshold";
        else if (stream->codecpar->color_range == AVCOL_RANGE_JPEG
                 || (stream->codecpar->format != AV_PIX_FMT_YUV420P
                     && stream->codecpar->format != AV_PIX_FMT_NONE))
            impl_->diagnostics.fallbackReason = "non-standard color format uses validated CPU path";
        else if (stream->codecpar->field_order != AV_FIELD_UNKNOWN
                 && stream->codecpar->field_order != AV_FIELD_PROGRESSIVE)
            impl_->diagnostics.fallbackReason = "interlaced source uses validated CPU path";
        else if (std::abs(impl_->metadata.sampleAspectRatio - 1.0) > 1e-6
                 || std::abs(impl_->metadata.rotationDegrees) > 1e-6)
            impl_->diagnostics.fallbackReason = "rotated/anamorphic source uses display-correct CPU path";
        else if (options.resizeOnNvidia && (options.maxWidth < 2 || options.maxHeight < 2))
            impl_->diagnostics.fallbackReason = "hardware analysis requires working dimensions";
        else {
            const double scale = std::min({1.0, double(options.maxWidth) / impl_->metadata.width,
                                           double(options.maxHeight) / impl_->metadata.height});
            const int width = std::max(2, static_cast<int>(impl_->metadata.width * scale) / 2 * 2);
            const int height = std::max(2, static_cast<int>(impl_->metadata.height * scale) / 2 * 2);
            result = impl_->openCodec(hardwareDecoder, true, width, height);
            openedHardware = result >= 0;
            if (!openedHardware) impl_->diagnostics.fallbackReason = "NVDEC unavailable: " + ffError(result);
        }
    }
    if (!openedHardware && (result = impl_->openCodec(decoder, false)) < 0) {
        close(); throw std::runtime_error("open decoder: " + ffError(result));
    }
}

void VideoDecoder::close() noexcept { if (impl_) impl_->reset(); }
bool VideoDecoder::isOpen() const noexcept { return impl_ && impl_->codec != nullptr; }
const VideoInfo& VideoDecoder::info() const { if (!isOpen()) throw std::logic_error("decoder is not open"); return impl_->metadata; }
VideoDecodeDiagnostics VideoDecoder::diagnostics() const { return impl_->diagnostics; }

void VideoDecoder::setRgbaMaxDimensions(int maxWidth, int maxHeight) noexcept
{
    if (!impl_) return;
    impl_->rgbaMaxWidth = std::max(0, maxWidth);
    impl_->rgbaMaxHeight = std::max(0, maxHeight);
    if (impl_->scaler) {
        sws_freeContext(impl_->scaler);
        impl_->scaler = nullptr;
        impl_->scalerSourceWidth = 0;
        impl_->scalerSourceHeight = 0;
        impl_->scalerTargetWidth = 0;
        impl_->scalerTargetHeight = 0;
        impl_->scalerFormat = AV_PIX_FMT_NONE;
    }
}

bool VideoDecoder::readNext(DecodedFrame& output, bool convertToRgba)
{
    if (!isOpen()) throw std::logic_error("decoder is not open");
    ElapsedMeasurement measurement{impl_->diagnostics.readMilliseconds};
    // A timestamp-only caller can ask for the current frame later. Release it
    // just before receiving the next one, which keeps AVFrame ownership fully
    // inside this RAII wrapper.
    if (impl_->frameAvailable) {
        av_frame_unref(impl_->decoded);
        impl_->frameAvailable = false;
    }
    for (;;) {
        int result = avcodec_receive_frame(impl_->codec, impl_->decoded);
        if (result == 0) {
            ++impl_->diagnostics.decodedFrames;
            const AVFrame* source = impl_->decoded;
            if (source->width <= 0 || source->height <= 0
                || static_cast<std::uint64_t>(source->width)
                    * static_cast<std::uint64_t>(source->height) > kMaxDecodedFramePixels) {
                throw std::runtime_error("decoded frame dimensions exceed the safe limit");
            }
            output.width = source->width;
            output.height = source->height;
            const AVStream* stream = impl_->format->streams[impl_->streamIndex];
            const int64_t pts = source->best_effort_timestamp == AV_NOPTS_VALUE ? 0 : source->best_effort_timestamp;
            output.timestampSeconds = pts * av_q2d(stream->time_base);
            if (impl_->hardware && source->color_range == AVCOL_RANGE_JPEG) {
                impl_->fallBackToCpu("full-range frame uses validated CPU conversion",
                                    output.timestampSeconds, true);
                continue;
            }
            if (std::isfinite(impl_->recoveryTarget)) {
                const bool skip = impl_->recoveryIncludeTarget
                    ? output.timestampSeconds + 1e-6 < impl_->recoveryTarget
                    : output.timestampSeconds <= impl_->recoveryTarget + 1e-6;
                if (skip) { av_frame_unref(impl_->decoded); continue; }
                impl_->recoveryTarget = -std::numeric_limits<double>::infinity();
            }
            if (!convertToRgba) {
                output.rgba.clear();
                impl_->frameAvailable = true;
                impl_->lastDelivered = output.timestampSeconds;
                return true;
            }
            try { impl_->copyCurrentFrameToRgba(output); }
            catch (const std::exception& error) {
                if (!impl_->hardware) throw;
                impl_->fallBackToCpu(error.what(), output.timestampSeconds, true);
                continue;
            }
            impl_->lastDelivered = output.timestampSeconds;
            av_frame_unref(impl_->decoded);
            return true;
        }
        if (result != AVERROR(EAGAIN) && result != AVERROR_EOF && impl_->hardware) {
            impl_->fallBackToCpu("NVDEC decode failed: " + ffError(result), impl_->lastDelivered, false);
            continue;
        }
        if (result != AVERROR(EAGAIN) && result != AVERROR_EOF)
            throw std::runtime_error("decode frame: " + ffError(result));
        if (result == AVERROR_EOF) return false;
        if (!impl_->draining) {
            result = av_read_frame(impl_->format, impl_->packet);
            if (result == AVERROR_EOF) { avcodec_send_packet(impl_->codec, nullptr); impl_->draining = true; continue; }
            if (result < 0) throw std::runtime_error("read packet: " + ffError(result));
            if (impl_->packet->stream_index == impl_->streamIndex) {
                result = avcodec_send_packet(impl_->codec, impl_->packet);
                av_packet_unref(impl_->packet);
                if (result < 0 && result != AVERROR(EAGAIN) && impl_->hardware) {
                    impl_->fallBackToCpu("NVDEC packet failed: " + ffError(result), impl_->lastDelivered, false);
                    continue;
                }
                if (result < 0 && result != AVERROR(EAGAIN)) throw std::runtime_error("send packet: " + ffError(result));
            } else av_packet_unref(impl_->packet);
        }
    }
}

bool VideoDecoder::convertCurrentFrameToRgba(DecodedFrame& output)
{
    if (!isOpen() || !impl_->frameAvailable) return false;
    try { impl_->copyCurrentFrameToRgba(output); }
    catch (const std::exception& error) {
        if (!impl_->hardware) throw;
        impl_->fallBackToCpu(error.what(), impl_->lastDelivered, true);
        return readNext(output, true);
    }
    return true;
}

void VideoDecoder::seek(double timestampSeconds)
{
    if (!isOpen()) throw std::logic_error("decoder is not open");
    const AVStream* stream = impl_->format->streams[impl_->streamIndex];
    const auto timestamp = seekTimestamp(timestampSeconds, stream->time_base);
    if (impl_->frameAvailable) {
        av_frame_unref(impl_->decoded);
        impl_->frameAvailable = false;
    }
    const int result = av_seek_frame(impl_->format, impl_->streamIndex, timestamp, AVSEEK_FLAG_BACKWARD);
    if (result < 0) throw std::runtime_error("seek: " + ffError(result));
    avcodec_flush_buffers(impl_->codec); impl_->draining = false;
    impl_->lastDelivered = impl_->recoveryTarget = -std::numeric_limits<double>::infinity();
}

void VideoDecoder::rewind() { seek(0.0); }

} // namespace pfcore
