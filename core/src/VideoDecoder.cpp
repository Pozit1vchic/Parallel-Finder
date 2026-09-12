#include "pfcore/VideoDecoder.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/display.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
#include <libswscale/swscale.h>
}

namespace pfcore {
namespace {

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

} // namespace

struct VideoDecoder::Impl {
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* decoded = nullptr;
    SwsContext* scaler = nullptr;
    int streamIndex = -1;
    VideoInfo metadata;
    bool draining = false;

    ~Impl() { reset(); }

    void reset() noexcept
    {
        if (scaler) sws_freeContext(scaler);
        if (decoded) av_frame_free(&decoded);
        if (packet) av_packet_free(&packet);
        if (codec) avcodec_free_context(&codec);
        if (format) avformat_close_input(&format);
        scaler = nullptr;
        streamIndex = -1;
        draining = false;
        metadata = {};
    }
};

VideoDecoder::VideoDecoder() : impl_(std::make_unique<Impl>()) {}
VideoDecoder::~VideoDecoder() = default;
VideoDecoder::VideoDecoder(VideoDecoder&&) noexcept = default;
VideoDecoder& VideoDecoder::operator=(VideoDecoder&&) noexcept = default;

void VideoDecoder::open(const std::string& path)
{
    close();
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
    if ((result = avcodec_open2(impl_->codec, decoder, nullptr)) < 0) {
        close(); throw std::runtime_error("open decoder: " + ffError(result));
    }
    impl_->packet = av_packet_alloc();
    impl_->decoded = av_frame_alloc();
    if (!impl_->packet || !impl_->decoded) { close(); throw std::runtime_error("allocate decode buffers failed"); }

    impl_->metadata.width = impl_->codec->width;
    impl_->metadata.height = impl_->codec->height;
    impl_->metadata.durationSeconds = stream->duration == AV_NOPTS_VALUE
        ? (format->duration == AV_NOPTS_VALUE ? 0.0 : format->duration / static_cast<double>(AV_TIME_BASE))
        : stream->duration * av_q2d(stream->time_base);
    const AVRational rate = stream->avg_frame_rate.num ? stream->avg_frame_rate : stream->r_frame_rate;
    impl_->metadata.frameRate = rationalOr(rate, 0.0);
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
}

void VideoDecoder::close() noexcept { if (impl_) impl_->reset(); }
bool VideoDecoder::isOpen() const noexcept { return impl_ && impl_->codec != nullptr; }
const VideoInfo& VideoDecoder::info() const { if (!isOpen()) throw std::logic_error("decoder is not open"); return impl_->metadata; }

bool VideoDecoder::readNext(DecodedFrame& output)
{
    if (!isOpen()) throw std::logic_error("decoder is not open");
    for (;;) {
        int result = avcodec_receive_frame(impl_->codec, impl_->decoded);
        if (result == 0) {
            const AVFrame* source = impl_->decoded;
            if (!impl_->scaler) {
                impl_->scaler = sws_getContext(source->width, source->height, static_cast<AVPixelFormat>(source->format),
                    source->width, source->height, AV_PIX_FMT_RGBA, SWS_BILINEAR, nullptr, nullptr, nullptr);
                if (!impl_->scaler) throw std::runtime_error("create pixel converter failed");
            }
            output.width = source->width; output.height = source->height;
            output.rgba.resize(static_cast<std::size_t>(output.width) * output.height * 4);
            std::uint8_t* dst[] = { output.rgba.data() }; int stride[] = { output.width * 4 };
            sws_scale(impl_->scaler, source->data, source->linesize, 0, source->height, dst, stride);
            const AVStream* stream = impl_->format->streams[impl_->streamIndex];
            const int64_t pts = source->best_effort_timestamp == AV_NOPTS_VALUE ? 0 : source->best_effort_timestamp;
            output.timestampSeconds = pts * av_q2d(stream->time_base);
            av_frame_unref(impl_->decoded);
            return true;
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
                if (result < 0 && result != AVERROR(EAGAIN)) throw std::runtime_error("send packet: " + ffError(result));
            } else av_packet_unref(impl_->packet);
        }
    }
}

void VideoDecoder::seek(double timestampSeconds)
{
    if (!isOpen()) throw std::logic_error("decoder is not open");
    const AVStream* stream = impl_->format->streams[impl_->streamIndex];
    const int64_t timestamp = static_cast<int64_t>(std::max(0.0, timestampSeconds) / av_q2d(stream->time_base));
    const int result = av_seek_frame(impl_->format, impl_->streamIndex, timestamp, AVSEEK_FLAG_BACKWARD);
    if (result < 0) throw std::runtime_error("seek: " + ffError(result));
    avcodec_flush_buffers(impl_->codec); impl_->draining = false;
}

void VideoDecoder::rewind() { seek(0.0); }

} // namespace pfcore
