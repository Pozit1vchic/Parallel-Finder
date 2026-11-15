#include <pfcore/VideoDecoder.hpp>
#include <pfcore/SceneDetector.hpp>
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {

using Thumbnail = std::array<std::uint8_t, 64 * 36 * 4>;

Thumbnail thumbnail(const pfcore::DecodedFrame& frame)
{
    Thumbnail result{};
    for (int y = 0; y < 36; ++y) for (int x = 0; x < 64; ++x) {
        const auto source = (static_cast<std::size_t>(y * frame.height / 36) * frame.width
            + x * frame.width / 64) * 4;
        std::copy_n(frame.rgba.begin() + source, 4, result.begin() + (y * 64 + x) * 4);
    }
    return result;
}

int compareResize(const std::string& source, pfcore::VideoDecodeOptions options, double fps, double limit)
{
    pfcore::VideoDecoder cpu, gpu;
    options.preferNvidia = false;
    cpu.open(source, options);
    options.preferNvidia = options.resizeOnNvidia = true;
    gpu.open(source, options);
    if (gpu.diagnostics().backend != "nvdec")
        throw std::runtime_error("NVIDIA resize unavailable: " + gpu.diagnostics().fallbackReason);
    std::vector<double> times;
    std::vector<Thumbnail> cpuImages, gpuImages;
    pfcore::DecodedFrame a, b;
    double next = -std::numeric_limits<double>::infinity();
    double totalError = 0, maximumSampleError = 0;
    std::size_t frames = 0;
    while (cpu.readNext(a, false)) {
        if (limit > 0 && a.timestampSeconds >= limit) break;
        if (!gpu.readNext(b, false) || std::abs(a.timestampSeconds - b.timestampSeconds) > 1e-9)
            throw std::runtime_error("Decoded frame timestamps differ at frame " + std::to_string(frames));
        ++frames;
        if (a.timestampSeconds + 1e-9 < next) continue;
        if (!cpu.convertCurrentFrameToRgba(a) || !gpu.convertCurrentFrameToRgba(b)
            || a.width != b.width || a.height != b.height)
            throw std::runtime_error("Incompatible decoded comparison frames");
        times.push_back(a.timestampSeconds);
        cpuImages.push_back(thumbnail(a));
        gpuImages.push_back(thumbnail(b));
        double sampleError = 0;
        for (std::size_t i = 0; i < Thumbnail{}.size(); i += 4)
            for (std::size_t channel = 0; channel < 3; ++channel)
                sampleError += std::abs(int(cpuImages.back()[i+channel]) - int(gpuImages.back()[i+channel]));
        sampleError /= 64 * 36 * 3;
        totalError += sampleError;
        maximumSampleError = std::max(maximumSampleError, sampleError);
        if (!std::isfinite(next)) next = a.timestampSeconds;
        do { next += 1.0 / fps; } while (next <= a.timestampSeconds + 1e-9);
    }
    if (times.empty() || (limit == 0 && gpu.readNext(b, false)))
        throw std::runtime_error("Empty or unequal decode streams");
    const auto cuts = [&](const std::vector<Thumbnail>& images) {
        std::vector<pfcore::SceneSample> samples;
        samples.reserve(times.size());
        for (std::size_t i = 0; i < times.size(); ++i) samples.push_back({times[i], 64, 36, images[i]});
        return pfcore::SceneDetector().detect(samples);
    };
    const auto cpuCuts = cuts(cpuImages), gpuCuts = cuts(gpuImages);
    std::cout << "frames=" << frames << " samples=" << times.size()
        << " thumbnail_mean_rgb_error=" << totalError / times.size()
        << " thumbnail_max_sample_rgb_error=" << maximumSampleError
        << " cpu_cuts=" << cpuCuts.size() << " gpu_cuts=" << gpuCuts.size() << '\n';
    const auto printMissing = [](const auto& left, const auto& right, const char* label) {
        for (const auto& cut : left)
            if (std::none_of(right.begin(), right.end(), [&](const auto& other) {
                return cut.timestampSeconds == other.timestampSeconds;
            })) std::cout << label << cut.timestampSeconds << '\n';
    };
    printMissing(cpuCuts, gpuCuts, "cpu_only_cut_seconds=");
    printMissing(gpuCuts, cpuCuts, "gpu_only_cut_seconds=");
    return 0;
}

} // namespace

// Isolate the application's actual decoder from inference and GUI costs.
// No proxy files, model calls or skipped bitstream reference frames.
#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
#else
int main(int argc, char** argv)
#endif
{
    try {
        if (argc < 3 || argc > 7) {
            std::cerr << "Usage: pf_decode_probe VIDEO cpu|nvdec|nvdec-resize|compare-resize|nvdec-boundaries|nvdec-boundaries-parallel [THREADS=0] [SAMPLE_FPS=6] [DURATION_SECONDS=0] [CONVERSION_THREADS=1; parallel boundary mode: WORKERS=2]\n";
            return 2;
        }
        const auto utf8Argument = [&](int index) {
            const auto bytes = std::filesystem::path(argv[index]).u8string();
            return std::string(bytes.begin(), bytes.end());
        };
        const auto source = utf8Argument(1);
        const auto mode = utf8Argument(2);
        const bool parallelBoundaries=mode=="nvdec-boundaries-parallel";
        if (mode != "cpu" && mode != "nvdec" && mode != "nvdec-resize" && mode != "compare-resize" && mode != "nvdec-boundaries" && !parallelBoundaries) throw std::invalid_argument("invalid decode mode");
        pfcore::VideoDecodeOptions options;
        options.preferNvidia = mode != "cpu";
        options.resizeOnNvidia = mode == "nvdec-resize";
        options.threads = argc > 3 ? std::stoi(argv[3]) : 0;
        options.conversionThreads=!parallelBoundaries && argc>6 ? std::stoi(argv[6]):1;
        options.maxWidth = 1280;
        options.maxHeight = 720;
        const double fps = argc > 4 ? std::stod(argv[4]) : 6.0;
        if (!std::isfinite(fps) || fps <= 0 || fps > 240) throw std::invalid_argument("invalid sample FPS");
        const double durationLimit=argc>5 ? std::stod(argv[5]) : 0.;
        if(!std::isfinite(durationLimit) || durationLimit<0)throw std::invalid_argument("invalid duration limit");
        if (mode == "compare-resize") return compareResize(source, options, fps, durationLimit);
        const auto start = std::chrono::steady_clock::now();
        pfcore::VideoDecoder decoder;
        decoder.open(source, options);
        if (mode == "nvdec-boundaries" || parallelBoundaries) {
            // Fixed adjacent sampled brackets exercise the production native
            // refinement, without model work or a user-cache dependency.
            const double limit = std::min(decoder.info().durationSeconds,
                durationLimit > 0 ? durationLimit : 600.);
            std::vector<pfcore::SceneBoundary> boundaries;
            std::vector<double> sampleTimes, poseTimes;
            for (double time = 0; time < limit; time += .25) sampleTimes.push_back(time);
            for (double time = 0; time < limit; time += 1. / 6) poseTimes.push_back(time);
            for (double time = 1.25; time + 1 < limit; time += 5)
                boundaries.push_back({time, 1});
            const auto refined = parallelBoundaries
                ? pfcore::SceneDetector().refineSupportedBoundariesParallel(source,options,
                    boundaries,sampleTimes,poseTimes,argc>6 ? std::stoi(argv[6]):2)
                : pfcore::SceneDetector().refineSupportedBoundaries(decoder,boundaries,sampleTimes,poseTimes);
            std::uint64_t fingerprint = 14695981039346656037ULL;
            for (const auto& cut : refined) for (double value : {cut.timestampSeconds, cut.score}) {
                fingerprint ^= std::bit_cast<std::uint64_t>(value);
                fingerprint *= 1099511628211ULL;
            }
            std::cout << "boundaries=" << refined.size() << " fingerprint=" << fingerprint
                << " workers=" << (parallelBoundaries ? (argc>6 ? std::stoi(argv[6]):2):1)
                << " elapsed_ms=" << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count()
                << '\n';
            return refined.size() == boundaries.size() ? 0 : 1;
        }
        pfcore::DecodedFrame frame;
        std::uint64_t frames = 0, samples = 0, pixels = 0;
        std::uint64_t sampledTimestamps = 14695981039346656037ULL;
        double next = -std::numeric_limits<double>::infinity();
        while (decoder.readNext(frame, false)) {
            if(durationLimit>0 && frame.timestampSeconds>=durationLimit)break;
            ++frames;
            if (frame.timestampSeconds + 1e-9 < next) continue;
            if (!decoder.convertCurrentFrameToRgba(frame)) throw std::runtime_error("missing retained frame");
            ++samples;
            sampledTimestamps ^= std::bit_cast<std::uint64_t>(frame.timestampSeconds);
            sampledTimestamps *= 1099511628211ULL;
            pixels += static_cast<std::uint64_t>(frame.width) * frame.height;
            if (!std::isfinite(next)) next = frame.timestampSeconds + 1.0 / fps;
            else do { next += 1.0 / fps; } while (next <= frame.timestampSeconds + 1e-9);
        }
        const auto stats = decoder.diagnostics();
        const auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count();
        std::cout << "backend=" << stats.backend << " threads=" << stats.threads
            << " conversion_threads=" << stats.conversionThreads
            << " source=" << decoder.info().width << 'x' << decoder.info().height
            << " frames=" << frames << " samples=" << samples << " pixels=" << pixels
            << " sampled_timestamps=" << sampledTimestamps
            << " downloads=" << stats.hardwareDownloads << " elapsed_ms=" << elapsed
            << " reused_buffers=" << stats.reusedDownloadBuffers << " download_ms=" << stats.downloadMilliseconds
            << " read_ms=" << stats.readMilliseconds << " convert_ms=" << stats.conversionMilliseconds
            << " fallback=" << stats.fallbackReason << '\n';
        return frames ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
