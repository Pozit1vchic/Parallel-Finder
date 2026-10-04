#include <pfcore/SceneDetector.hpp>
#include <pfcore/VideoDecoder.hpp>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

// Reproduce the analysis scene clock and thumbnails on a source interval.
// This is a diagnostic, not a performance benchmark or an edited video.
int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    try {
        const auto arguments = application.arguments();
        if (arguments.size() != 4) {
            std::cerr << "Usage: pf_scene_probe VIDEO START END\n";
            return 2;
        }
        bool startOk = false, endOk = false;
        const double start = arguments[2].toDouble(&startOk);
        const double end = arguments[3].toDouble(&endOk);
        if (!startOk || !endOk || !std::isfinite(start) || !std::isfinite(end)
            || start < 0 || end <= start) throw std::invalid_argument("invalid interval");
        pfcore::VideoDecodeOptions options;
        options.threads = 2;
        options.maxWidth = 1280;
        options.maxHeight = 720;
        pfcore::VideoDecoder decoder;
        decoder.open(arguments[1].toStdString(), options);
        decoder.seek(start);
        std::vector<std::vector<std::uint8_t>> thumbnails;
        std::vector<double> times;
        pfcore::DecodedFrame frame;
        double next = std::ceil(start * 4.0) / 4.0;
        while (decoder.readNext(frame, false)) {
            if (frame.timestampSeconds >= end) break;
            if (frame.timestampSeconds + 1e-9 < next) continue;
            if (!decoder.convertCurrentFrameToRgba(frame))
                throw std::runtime_error("missing retained frame");
            std::vector<std::uint8_t> thumbnail(64 * 36 * 4);
            for (int y = 0; y < 36; ++y) {
                for (int x = 0; x < 64; ++x) {
                    const auto offset = (static_cast<std::size_t>(y * frame.height / 36)
                        * frame.width + x * frame.width / 64) * 4;
                    std::copy_n(frame.rgba.begin() + static_cast<std::ptrdiff_t>(offset), 4,
                        thumbnail.begin() + (y * 64 + x) * 4);
                }
            }
            thumbnails.push_back(std::move(thumbnail));
            times.push_back(frame.timestampSeconds);
            do { next += .25; } while (next <= frame.timestampSeconds + 1e-9);
        }
        std::vector<pfcore::SceneSample> samples;
        for (std::size_t i = 0; i < times.size(); ++i)
            samples.push_back({times[i], 64, 36, thumbnails[i]});
        const auto minSamples = static_cast<std::size_t>(std::ceil(8 * 4.0
            / std::max(1.0, decoder.info().frameRate)));
        const auto boundaries = pfcore::SceneDetector(27, minSamples, 3).detect(samples);
        QJsonArray cuts, timestamps;
        for (const auto cut : boundaries)
            cuts.append(QJsonObject{{"time", cut.timestampSeconds}, {"score", cut.score}});
        for (const auto time : times) timestamps.append(time);
        std::cout << QJsonDocument(QJsonObject{{"source", arguments[1]},
            {"start", start}, {"end", end}, {"samples", timestamps}, {"cuts", cuts},
            {"policy", "Actual decoder and analysis thumbnails; interval-local adaptive history"}})
            .toJson(QJsonDocument::Compact).constData() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
