#include <gtest/gtest.h>

#include <pfcore/VideoDecoder.hpp>
#include <pfcore/VideoSampleReader.hpp>

#include <filesystem>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <atomic>
#include <chrono>
#include <thread>

namespace {

QString h264Fixture(QTemporaryDir& directory, const QStringList& extra = {})
{
    const auto ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    if (ffmpeg.isEmpty()) return {};
    const auto path = directory.filePath("h264.mp4");
    QProcess generator;
    QStringList arguments{"-hide_banner", "-loglevel", "error", "-y",
        "-f", "lavfi", "-i", "testsrc2=size=160x96:rate=24:duration=2",
        "-c:v", "libx264", "-preset", "veryfast", "-g", "24"};
    arguments.append(extra);
    arguments.append(path);
    generator.start(ffmpeg, arguments);
    if (!generator.waitForFinished(30000) || generator.exitCode() != 0) return {};
    return path;
}

TEST(VideoDecoder, OpensFixtureReadsFramesAndSeeks)
{
    pfcore::VideoDecoder decoder;
    decoder.open((std::filesystem::path(PF_TEST_FIXTURE_DIR) / "tiny.mp4").string());
    ASSERT_TRUE(decoder.isOpen());
    EXPECT_EQ(decoder.info().width, 16);
    EXPECT_EQ(decoder.info().height, 12);
    EXPECT_GT(decoder.info().frameRate, 0.0);

    pfcore::DecodedFrame first;
    ASSERT_TRUE(decoder.readNext(first));
    EXPECT_EQ(first.width, 16);
    EXPECT_EQ(first.height, 12);
    EXPECT_EQ(first.rgba.size(), 16U * 12U * 4U);

    decoder.seek(0.5);
    pfcore::DecodedFrame afterSeek;
    ASSERT_TRUE(decoder.readNext(afterSeek));
    EXPECT_GE(afterSeek.timestampSeconds, 0.0);
}

TEST(VideoDecoder, RejectsMissingFile)
{
    pfcore::VideoDecoder decoder;
    EXPECT_THROW(decoder.open("this-file-does-not-exist.mp4"), std::runtime_error);
}

TEST(VideoSampleReader, PrefetchPreservesSerialSamplesPixelsAndTailProgress)
{
    QTemporaryDir directory;
    const auto path = h264Fixture(directory,
        {"-vf", "setpts=(N+floor(N/3))/24/TB", "-fps_mode", "vfr"});
    if (path.isEmpty()) GTEST_SKIP() << "FFmpeg fixture generator unavailable";
    for (const auto fps : {6.0, 10.0, 15.0}) {
        pfcore::VideoDecoder serial, async;
        serial.open(path.toStdString());
        async.open(path.toStdString());
        serial.setRgbaMaxDimensions(128, 72);
        async.setRgbaMaxDimensions(128, 72);
        pfcore::VideoSampleReader a(serial, fps, 4.0, {}, false);
        pfcore::VideoSampleReader b(async, fps, 4.0, {}, true, 1);
        pfcore::VideoSample left, right;
        std::uint64_t frames = 0;
        std::size_t poseSamples = 0, sceneSamples = 0;
        while (a.readNext(left)) {
            ASSERT_TRUE(b.readNext(right));
            EXPECT_EQ(left.frame.timestampSeconds, right.frame.timestampSeconds);
            EXPECT_EQ(left.frame.width, right.frame.width);
            EXPECT_EQ(left.frame.height, right.frame.height);
            EXPECT_EQ(left.frame.rgba, right.frame.rgba);
            EXPECT_EQ(left.pose, right.pose);
            EXPECT_EQ(left.scene, right.scene);
            EXPECT_EQ(left.decodedFrames, right.decodedFrames);
            EXPECT_GT(left.decodedFrames, frames);
            frames = left.decodedFrames;
            poseSamples += left.pose;
            sceneSamples += left.scene;
        }
        EXPECT_FALSE(b.readNext(right));
        EXPECT_FALSE(b.readNext(right)); // repeated EOF is safe
        EXPECT_EQ(frames, 48u);
        EXPECT_GT(poseSamples, 8u);
        EXPECT_GT(sceneSamples, 5u);
        EXPECT_LE(b.peakBufferedSamples(), 1u);
        EXPECT_EQ(async.diagnostics().decodedFrames, serial.diagnostics().decodedFrames);
        EXPECT_EQ(async.diagnostics().convertedFrames, serial.diagnostics().convertedFrames);
    }
}

TEST(VideoSampleReader, TimestampGridMatchesIndependentReference)
{
    const auto path = (std::filesystem::path(PF_TEST_FIXTURE_DIR) / "tiny.mp4").string();
    pfcore::VideoDecoder reference, async;
    reference.open(path);
    async.open(path);
    pfcore::VideoSampleReader reader(async, 6.0, 4.0);
    double nextPose = -1, nextScene = -1;
    pfcore::DecodedFrame frame;
    pfcore::VideoSample sample;
    while (reference.readNext(frame)) {
        const bool pose = frame.timestampSeconds + 1e-9 >= nextPose;
        const bool scene = frame.timestampSeconds + 1e-9 >= nextScene;
        if (pose) {
            if (nextPose < 0) nextPose = frame.timestampSeconds + 1.0 / 6.0;
            else do { nextPose += 1.0 / 6.0; } while (nextPose <= frame.timestampSeconds + 1e-9);
        }
        if (scene) {
            if (nextScene < 0) nextScene = frame.timestampSeconds + 0.25;
            else do { nextScene += 0.25; } while (nextScene <= frame.timestampSeconds + 1e-9);
        }
        if (!pose && !scene) continue;
        ASSERT_TRUE(reader.readNext(sample));
        EXPECT_EQ(sample.pose, pose);
        EXPECT_EQ(sample.scene, scene);
        EXPECT_EQ(sample.frame.timestampSeconds, frame.timestampSeconds);
        EXPECT_EQ(sample.frame.rgba, frame.rgba);
    }
    while (reader.readNext(sample)) {
        EXPECT_FALSE(sample.pose);
        EXPECT_FALSE(sample.scene);
        EXPECT_TRUE(sample.frame.rgba.empty());
    }
}

TEST(VideoSampleReader, CancelsFullQueueWithoutDeadlockAndBoundsMemory)
{
    const auto path = (std::filesystem::path(PF_TEST_FIXTURE_DIR) / "tiny.mp4").string();
    pfcore::VideoDecoder decoder;
    decoder.open(path);
    std::atomic_bool cancelled{false};
    pfcore::VideoSampleReader reader(decoder, 1000.0, 4.0, [&] { return cancelled.load(); }, true, 1);
    const auto timeout = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (reader.peakBufferedSamples() == 0 && std::chrono::steady_clock::now() < timeout)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    EXPECT_EQ(reader.peakBufferedSamples(), 1u);
    const auto start = std::chrono::steady_clock::now();
    cancelled = true;
    reader.finish();
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(1));
    EXPECT_LE(decoder.diagnostics().convertedFrames, 2u); // one queued + one producer frame
}

TEST(VideoSampleReader, RejectsInvalidOptionsBeforeStartingWorker)
{
    pfcore::VideoDecoder decoder;
    EXPECT_THROW(pfcore::VideoSampleReader(decoder, 6, 4), std::invalid_argument);
    decoder.open((std::filesystem::path(PF_TEST_FIXTURE_DIR) / "tiny.mp4").string());
    EXPECT_THROW(pfcore::VideoSampleReader(decoder, -1, 4), std::invalid_argument);
    EXPECT_THROW(pfcore::VideoSampleReader(decoder, 1e300, 4), std::invalid_argument);
    EXPECT_THROW(pfcore::VideoSampleReader(decoder, 6, 4, {}, true, 0), std::invalid_argument);
    EXPECT_THROW(pfcore::VideoSampleReader(decoder, 6, 4, {}, true, 9), std::invalid_argument);
}

TEST(VideoSampleReader, PropagatesProducerFailureAndJoinsWorker)
{
    pfcore::VideoDecoder decoder;
    decoder.open((std::filesystem::path(PF_TEST_FIXTURE_DIR) / "tiny.mp4").string());
    pfcore::VideoSampleReader reader(decoder, 6, 4,
        []() -> bool { throw std::runtime_error("producer failure"); });
    pfcore::VideoSample sample;
    EXPECT_THROW(reader.readNext(sample), std::runtime_error);
    reader.finish();
    EXPECT_EQ(decoder.diagnostics().decodedFrames, 0u);
}

TEST(VideoSampleReader, WaitProfilingIsOptInAndDoesNotChangePixels)
{
    const auto path = (std::filesystem::path(PF_TEST_FIXTURE_DIR) / "tiny.mp4").string();
    pfcore::VideoDecoder reference; reference.open(path);
    pfcore::VideoSampleReader serial(reference, 6, 4, {}, false);
    std::vector<pfcore::VideoSample> expected;
    pfcore::VideoSample item;
    while (serial.readNext(item)) expected.push_back(std::move(item));
    for (bool profile : {false, true}) {
        pfcore::VideoDecoder decoder; decoder.open(path);
        pfcore::VideoSampleReader reader(decoder, 6, 4, {}, true, 1, profile);
        std::size_t index = 0;
        while (reader.readNext(item)) {
            ASSERT_LT(index, expected.size());
            EXPECT_EQ(item.frame.timestampSeconds, expected[index].frame.timestampSeconds);
            EXPECT_EQ(item.frame.rgba, expected[index].frame.rgba);
            EXPECT_EQ(item.pose, expected[index].pose); EXPECT_EQ(item.scene, expected[index].scene);
            ++index;
        }
        EXPECT_EQ(index, expected.size());
        const auto waits = reader.waitDiagnostics();
        EXPECT_TRUE(std::isfinite(waits.consumerMilliseconds)); EXPECT_GE(waits.consumerMilliseconds, 0);
        EXPECT_TRUE(std::isfinite(waits.producerMilliseconds)); EXPECT_GE(waits.producerMilliseconds, 0);
        if (!profile) {
            EXPECT_EQ(waits.consumerMilliseconds, 0); EXPECT_EQ(waits.producerMilliseconds, 0);
            EXPECT_EQ(waits.consumerWaits, 0u); EXPECT_EQ(waits.producerWaits, 0u);
        }
    }
}

TEST(VideoDecoder, RepeatedPreviewSeeksMatchFreshDecoder)
{
    const auto path = (std::filesystem::path(PF_TEST_FIXTURE_DIR) / "tiny.mp4").string();
    pfcore::VideoDecoder reused;
    reused.open(path);
    for (const double target : {0.6, 0.1, 0.8, 0.3}) {
        auto at = [target](pfcore::VideoDecoder& decoder) {
            decoder.seek(target);
            pfcore::DecodedFrame frame;
            while (decoder.readNext(frame, false)) {
                if (frame.timestampSeconds + 1e-3 >= target) {
                    EXPECT_TRUE(decoder.convertCurrentFrameToRgba(frame));
                    return frame;
                }
            }
            return frame;
        };
        pfcore::VideoDecoder fresh;
        fresh.open(path);
        const auto expected = at(fresh);
        const auto actual = at(reused);
        ASSERT_FALSE(expected.rgba.empty());
        EXPECT_EQ(actual.timestampSeconds, expected.timestampSeconds);
        EXPECT_EQ(actual.rgba, expected.rgba);
    }
}

TEST(VideoDecoder, BoundedThreadingPreservesFramesAndResolutionCeiling)
{
    const auto path = (std::filesystem::path(PF_TEST_FIXTURE_DIR) / "tiny.mp4").string();
    pfcore::VideoDecodeOptions single;
    single.threads = 1;
    single.maxWidth = 8;
    single.maxHeight = 8;
    auto parallel = single;
    parallel.threads = 256;
    pfcore::VideoDecoder a, b;
    a.open(path, single);
    b.open(path, parallel);
    EXPECT_EQ(a.diagnostics().threads, 1);
    EXPECT_EQ(b.diagnostics().threads, 8);
    pfcore::DecodedFrame left, right;
    int count = 0;
    while (a.readNext(left)) {
        ASSERT_TRUE(b.readNext(right));
        EXPECT_EQ(left.timestampSeconds, right.timestampSeconds);
        EXPECT_EQ(left.rgba, right.rgba);
        EXPECT_LE(right.width, 8);
        EXPECT_LE(right.height, 8);
        ++count;
    }
    EXPECT_GT(count, 0);
    EXPECT_FALSE(b.readNext(right));
    EXPECT_EQ(b.info().width, 16); // original metadata, not resized output
    EXPECT_EQ(b.diagnostics().convertedFrames, static_cast<std::uint64_t>(count));
    b.close();
    EXPECT_EQ(b.diagnostics().decodedFrames, 0u);
}

TEST(VideoDecoder, UnsupportedHardwareCodecFallsBackWithoutLosingFrames)
{
    const auto path = (std::filesystem::path(PF_TEST_FIXTURE_DIR) / "tiny.mp4").string();
    pfcore::VideoDecodeOptions options;
    options.preferNvidia = true;
    options.maxWidth = options.maxHeight = 8;
    pfcore::VideoDecoder decoder;
    decoder.open(path, options); // fixture is MPEG4, not our CUVID codec allow-list
    EXPECT_EQ(decoder.diagnostics().backend, "cpu");
    EXPECT_FALSE(decoder.diagnostics().fallbackReason.empty());
    pfcore::DecodedFrame frame;
    int count = 0;
    while (decoder.readNext(frame)) ++count;
    EXPECT_GT(count, 0);
    EXPECT_EQ(decoder.diagnostics().hardwareDownloads, 0u);
}

TEST(VideoDecoder, UnavailableNvidiaDeviceUsesIdenticalSoftwareFrames)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto path = h264Fixture(directory);
    if (path.isEmpty()) GTEST_SKIP() << "FFmpeg fixture generator unavailable";
    pfcore::VideoDecodeOptions cpu;
    cpu.maxWidth = 128;
    cpu.maxHeight = 72;
    auto gpu = cpu;
    gpu.preferNvidia = true;
    gpu.nvidiaDevice = 32767;
    pfcore::VideoDecoder a, b;
    a.open(path.toStdString(), cpu);
    b.open(path.toStdString(), gpu);
    EXPECT_EQ(b.diagnostics().backend, "cpu");
    EXPECT_FALSE(b.diagnostics().fallbackReason.empty());
    pfcore::DecodedFrame left, right;
    int count = 0;
    while (a.readNext(left)) {
        ASSERT_TRUE(b.readNext(right));
        EXPECT_EQ(left.timestampSeconds, right.timestampSeconds);
        EXPECT_EQ(left.rgba, right.rgba);
        ++count;
    }
    EXPECT_EQ(count, 48);
    EXPECT_FALSE(b.readNext(right));
}

TEST(VideoDecoder, RotatedHardwareRequestPreservesDisplayGeometryOnCpu)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto source = h264Fixture(directory);
    if (source.isEmpty()) GTEST_SKIP() << "FFmpeg fixture generator unavailable";
    const auto path = directory.filePath("rotated.mp4");
    QProcess rotation;
    rotation.start(QStandardPaths::findExecutable("ffmpeg"), {"-hide_banner", "-loglevel", "error",
        "-y", "-display_rotation", "90", "-i", source, "-c", "copy", path});
    ASSERT_TRUE(rotation.waitForFinished(30000));
    ASSERT_EQ(rotation.exitCode(), 0);
    pfcore::VideoDecodeOptions options;
    options.preferNvidia = true;
    options.maxWidth = 128;
    options.maxHeight = 72;
    pfcore::VideoDecoder decoder;
    decoder.open(path.toStdString(), options);
    ASSERT_NE(decoder.info().rotationDegrees, 0.0);
    EXPECT_EQ(decoder.diagnostics().backend, "cpu");
    pfcore::DecodedFrame frame;
    ASSERT_TRUE(decoder.readNext(frame));
    EXPECT_GT(frame.height, frame.width);
    EXPECT_LE(frame.height, 72);
    EXPECT_LE(frame.width, 128);
}

TEST(VideoDecoder, ReducedNvidiaFramesPreserveTimestampsMetadataAndSelectedDownloads)
{
    const auto path = qEnvironmentVariable("PF_TEST_HW_SOURCE");
    if (path.isEmpty()) GTEST_SKIP() << "Opt-in real NVIDIA decoder integration";
    pfcore::VideoDecodeOptions options;
    options.maxWidth = 1280;
    options.maxHeight = 720;
    pfcore::VideoDecoder cpu, gpu;
    cpu.open(path.toStdString(), options);
    options.preferNvidia = true;
    options.resizeOnNvidia = true;
    gpu.open(path.toStdString(), options);
    if (qEnvironmentVariableIsSet("PF_TEST_REQUIRE_NVDEC"))
        ASSERT_EQ(gpu.diagnostics().backend, "nvdec") << gpu.diagnostics().fallbackReason;
    else if (gpu.diagnostics().backend != "nvdec")
        GTEST_SKIP() << gpu.diagnostics().fallbackReason;
    EXPECT_EQ(cpu.info().width, gpu.info().width);
    EXPECT_EQ(cpu.info().height, gpu.info().height);
    pfcore::DecodedFrame a, b;
    std::uint64_t frames = 0, samples = 0;
    while (cpu.readNext(a, false)) {
        ASSERT_TRUE(gpu.readNext(b, false));
        ASSERT_NEAR(a.timestampSeconds, b.timestampSeconds, 1e-6);
        if (frames % 10 == 0) {
            ASSERT_TRUE(cpu.convertCurrentFrameToRgba(a));
            ASSERT_TRUE(gpu.convertCurrentFrameToRgba(b));
            EXPECT_EQ(a.width, b.width);
            EXPECT_EQ(a.height, b.height);
            EXPECT_LE(b.width, 1280);
            EXPECT_LE(b.height, 720);
            ASSERT_EQ(a.rgba.size(), b.rgba.size());
            double difference = 0;
            for (std::size_t i = 0; i < a.rgba.size(); ++i)
                difference += std::abs(int(a.rgba[i]) - int(b.rgba[i]));
            EXPECT_LT(difference / a.rgba.size(), 12.0); // differing resize kernels, not bit-exact
            ++samples;
        }
        ++frames;
    }
    ASSERT_GT(frames, 0u);
    EXPECT_FALSE(gpu.readNext(b, false));
    EXPECT_EQ(gpu.diagnostics().hardwareDownloads, samples);
    EXPECT_EQ(gpu.diagnostics().convertedFrames, samples);
    EXPECT_EQ(gpu.diagnostics().decodedFrames, frames);
    const auto cpuStats = cpu.diagnostics(), gpuStats = gpu.diagnostics();
    std::printf("PF_DECODE_TEST frames=%llu samples=%llu cpu_read_ms=%.1f cpu_convert_ms=%.1f gpu_read_ms=%.1f gpu_convert_ms=%.1f\n",
        static_cast<unsigned long long>(frames), static_cast<unsigned long long>(samples),
        cpuStats.readMilliseconds, cpuStats.conversionMilliseconds,
        gpuStats.readMilliseconds, gpuStats.conversionMilliseconds);
    // Random-access preview semantics must survive hardware parser flushes.
    for (const double target : {0.6, 0.1, 0.8, 0.3}) {
        cpu.seek(target);
        gpu.seek(target);
        do { ASSERT_TRUE(cpu.readNext(a, false)); } while (a.timestampSeconds + 1e-6 < target);
        do { ASSERT_TRUE(gpu.readNext(b, false)); } while (b.timestampSeconds + 1e-6 < target);
        EXPECT_NEAR(a.timestampSeconds, b.timestampSeconds, 1e-6);
        ASSERT_TRUE(gpu.convertCurrentFrameToRgba(b));
        EXPECT_LE(b.width, 1280);
    }
}

TEST(VideoDecoder, InvalidOptionsFailBeforeOpeningSource)
{
    pfcore::VideoDecoder decoder;
    pfcore::VideoDecodeOptions options;
    options.threads = -1;
    EXPECT_THROW(decoder.open("missing.mp4", options), std::invalid_argument);
    EXPECT_FALSE(decoder.isOpen());
}

TEST(VideoDecoder, SmallSourceAvoidsUnnecessaryHardwareInitialization)
{
    QTemporaryDir directory;
    const auto path = h264Fixture(directory);
    if (path.isEmpty()) GTEST_SKIP() << "FFmpeg fixture generator unavailable";
    pfcore::VideoDecodeOptions options;
    options.preferNvidia = true;
    options.minimumNvidiaPixels = 1920ULL * 1080ULL + 1;
    options.nvidiaDevice = 32767; // size guard must run before device creation
    pfcore::VideoDecoder decoder;
    decoder.open(path.toStdString(), options);
    EXPECT_EQ(decoder.diagnostics().backend, "cpu");
    EXPECT_EQ(decoder.diagnostics().fallbackReason, "source below hardware size threshold");
    pfcore::DecodedFrame frame;
    int count = 0;
    while (decoder.readNext(frame)) ++count;
    EXPECT_EQ(count, 48);
}

TEST(VideoDecoder, AnamorphicHardwareRequestPreservesDisplayGeometryOnCpu)
{
    QTemporaryDir directory;
    const auto path = h264Fixture(directory, {"-vf", "setsar=2"});
    if (path.isEmpty()) GTEST_SKIP() << "FFmpeg fixture generator unavailable";
    pfcore::VideoDecodeOptions options;
    options.preferNvidia = true;
    options.maxWidth = 128;
    options.maxHeight = 72;
    pfcore::VideoDecoder decoder;
    decoder.open(path.toStdString(), options);
    ASSERT_NEAR(decoder.info().sampleAspectRatio, 2.0, 1e-6);
    EXPECT_EQ(decoder.diagnostics().backend, "cpu");
    pfcore::DecodedFrame frame;
    ASSERT_TRUE(decoder.readNext(frame));
    EXPECT_EQ(frame.width, 128);
    EXPECT_EQ(frame.height, 38);
}

TEST(VideoDecoder, NativeNvidiaDecodePreservesCpuWorkingPixelsIncludingFullRange)
{
    const auto realSource = qEnvironmentVariable("PF_TEST_HW_SOURCE");
    if (realSource.isEmpty()) GTEST_SKIP() << "Opt-in real NVIDIA decoder integration";
    QTemporaryDir directory;
    const auto fullRange = h264Fixture(directory, {"-pix_fmt", "yuvj420p", "-color_range", "pc"});
    ASSERT_FALSE(fullRange.isEmpty());
    QTemporaryDir vfrDirectory;
    const auto vfr = h264Fixture(vfrDirectory,
        {"-vf", "setpts=(N+floor(N/3))/24/TB", "-fps_mode", "vfr"});
    ASSERT_FALSE(vfr.isEmpty());
    for (const auto& path : {realSource, fullRange, vfr}) {
        pfcore::VideoDecodeOptions options;
        options.maxWidth = 1280;
        options.maxHeight = 720;
        pfcore::VideoDecoder cpu, gpu;
        cpu.open(path.toStdString(), options);
        options.preferNvidia = true; // GPU decode, same CPU resize as production
        gpu.open(path.toStdString(), options);
        ASSERT_EQ(gpu.diagnostics().backend, path == fullRange ? "cpu" : "nvdec")
            << gpu.diagnostics().fallbackReason;
        EXPECT_EQ(gpu.info().variableFrameRate, cpu.info().variableFrameRate);
        if (path == vfr) { EXPECT_TRUE(gpu.info().variableFrameRate); }
        pfcore::DecodedFrame a, b;
        std::uint64_t frames = 0, samples = 0;
        while (cpu.readNext(a, false)) {
            ASSERT_TRUE(gpu.readNext(b, false));
            ASSERT_NEAR(a.timestampSeconds, b.timestampSeconds, 1e-6);
            if (frames % 10 == 0) {
                ASSERT_TRUE(cpu.convertCurrentFrameToRgba(a));
                ASSERT_TRUE(gpu.convertCurrentFrameToRgba(b));
                EXPECT_EQ(a.width, b.width);
                EXPECT_EQ(a.height, b.height);
                ASSERT_EQ(a.rgba.size(), b.rgba.size());
                EXPECT_TRUE(a.rgba == b.rgba) << path.toStdString() << " at " << a.timestampSeconds;
                ++samples;
            }
            ++frames;
        }
        EXPECT_FALSE(gpu.readNext(b, false));
        EXPECT_EQ(gpu.diagnostics().hardwareDownloads, path == fullRange ? 0u : samples);
        EXPECT_EQ(gpu.diagnostics().decodedFrames, frames);
        if (path == fullRange || path == vfr) { EXPECT_EQ(frames, 48u); }
    }
}

} // namespace
