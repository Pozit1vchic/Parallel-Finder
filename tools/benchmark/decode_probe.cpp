#include <pfcore/VideoDecoder.hpp>
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
        if (argc < 3 || argc > 5) {
            std::cerr << "Usage: pf_decode_probe VIDEO cpu|nvdec|nvdec-resize [THREADS=0] [SAMPLE_FPS=6]\n";
            return 2;
        }
        const std::string mode = argv[2];
        if (mode != "cpu" && mode != "nvdec" && mode != "nvdec-resize") throw std::invalid_argument("invalid decode mode");
        pfcore::VideoDecodeOptions options;
        options.preferNvidia = mode != "cpu";
        options.resizeOnNvidia = mode == "nvdec-resize";
        options.threads = argc > 3 ? std::stoi(argv[3]) : 0;
        options.maxWidth = 1280;
        options.maxHeight = 720;
        const double fps = argc > 4 ? std::stod(argv[4]) : 6.0;
        if (!std::isfinite(fps) || fps <= 0 || fps > 240) throw std::invalid_argument("invalid sample FPS");
        const auto start = std::chrono::steady_clock::now();
        pfcore::VideoDecoder decoder;
        decoder.open(argv[1], options);
        pfcore::DecodedFrame frame;
        std::uint64_t frames = 0, samples = 0, pixels = 0;
        double next = -std::numeric_limits<double>::infinity();
        while (decoder.readNext(frame, false)) {
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
            << " source=" << decoder.info().width << 'x' << decoder.info().height
            << " frames=" << frames << " samples=" << samples << " pixels=" << pixels
            << " downloads=" << stats.hardwareDownloads << " elapsed_ms=" << elapsed
            << " read_ms=" << stats.readMilliseconds << " convert_ms=" << stats.conversionMilliseconds
            << " fallback=" << stats.fallbackReason << '\n';
        return frames ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
