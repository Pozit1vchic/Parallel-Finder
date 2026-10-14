#include <pfcore/VideoDecoder.hpp>
#include <pfcore/SceneDetector.hpp>
#include <bit>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

// Isolate the application's actual decoder from inference and GUI costs.
// No proxy files, model calls or skipped bitstream reference frames.
int main(int argc, char** argv)
{
    try {
        if (argc < 3 || argc > 7) {
            std::cerr << "Usage: pf_decode_probe VIDEO cpu|nvdec|nvdec-resize|nvdec-boundaries|nvdec-boundaries-parallel [THREADS=0] [SAMPLE_FPS=6] [DURATION_SECONDS=0] [CONVERSION_THREADS=1; parallel boundary mode: WORKERS=2]\n";
            return 2;
        }
        const std::string mode = argv[2];
        const bool parallelBoundaries=mode=="nvdec-boundaries-parallel";
        if (mode != "cpu" && mode != "nvdec" && mode != "nvdec-resize" && mode != "nvdec-boundaries" && !parallelBoundaries) throw std::invalid_argument("invalid decode mode");
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
        const auto start = std::chrono::steady_clock::now();
        pfcore::VideoDecoder decoder;
        decoder.open(argv[1], options);
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
                ? pfcore::SceneDetector().refineSupportedBoundariesParallel(argv[1],options,
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
        double next = -std::numeric_limits<double>::infinity();
        while (decoder.readNext(frame, false)) {
            if(durationLimit>0 && frame.timestampSeconds>=durationLimit)break;
            ++frames;
            if (frame.timestampSeconds + 1e-9 < next) continue;
            if (!decoder.convertCurrentFrameToRgba(frame)) throw std::runtime_error("missing retained frame");
            ++samples;
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
