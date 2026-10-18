#include "../../core/src/SampleClock.hpp"
#include <gtest/gtest.h>

#include <pfcore/VideoDecoder.hpp>
#include <pfcore/VideoSampleReader.hpp>
#include <pfcore/SceneDetector.hpp>
#include "../../ui/src/PreviewRequestOrder.h"

#include <filesystem>
#include <list>
#include <map>
#include <memory>
#include <numeric>
#include <cstdint>
#include <set>
#include <QProcess>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QCryptographicHash>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <atomic>
#include <chrono>
#include <thread>
#include <unordered_set>

namespace {

QString h264Fixture(QTemporaryDir& directory, const QStringList& extra = {}, double duration=2)
{
    const auto ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    if (ffmpeg.isEmpty()) return {};
    const auto path = directory.filePath("h264.mp4");
    QProcess generator;
    QStringList arguments{"-hide_banner", "-loglevel", "error", "-y",
        "-f", "lavfi", "-i", QStringLiteral("testsrc2=size=160x96:rate=24:duration=%1").arg(duration),
        "-c:v", "libx264", "-preset", "veryfast", "-g", "24"};
    arguments.append(extra);
    arguments.append(path);
    generator.start(ffmpeg, arguments);
    if (!generator.waitForFinished(30000) || generator.exitCode() != 0) return {};
    return path;
}

TEST(VideoDecoder, SceneViewBatchMatchesIndependentSeeksAndHonorsBounds)
{
    QTemporaryDir directory;
    const auto path=h264Fixture(directory,{"-bf","3"});
    if(path.isEmpty())GTEST_SKIP()<<"FFmpeg H264 unavailable";
    pfcore::VideoDecoder batch,reference;batch.open(path.toStdString());reference.open(path.toStdString());
    batch.setRgbaMaxDimensions(160,90);reference.setRgbaMaxDimensions(160,90);
    const std::vector<pfcore::SceneViewRequest> requests={{1.1,1,1.5},{.2,0,.5},{.2,0,.5},{.41,.4,.4101},{1.8,1.5,2}};
    std::size_t done=0,total=0;
    const auto actual=pfcore::sampleSceneViews(batch,requests,{},[&](auto d,auto t){done=d;total=t;});
    ASSERT_EQ(actual.size(),requests.size());EXPECT_EQ(done,total);EXPECT_EQ(total,requests.size());
    for(std::size_t i=0;i<requests.size();++i) {
        const auto& r=requests[i];reference.seek(r.target);pfcore::DecodedFrame f;std::vector<float> expected;
        while(reference.readNext(f,false)) {
            if(f.timestampSeconds+1e-6<r.target)continue;
            if(f.timestampSeconds<=r.end+1e-6 && reference.convertCurrentFrameToRgba(f))
                expected=pfcore::sceneViewDescriptor({f.timestampSeconds,f.width,f.height,f.rgba});
            break;
        }
        EXPECT_EQ(actual[i].pixels,expected)<<"request="<<i;
    }
    EXPECT_LT(batch.diagnostics().decodedFrames,reference.diagnostics().decodedFrames);
    bool cancelled=true;done=999;
    const auto stopped=pfcore::sampleSceneViews(batch,requests,[&]{return cancelled;},[&](auto d,auto){done=d;});
    EXPECT_EQ(done,0U);for(const auto& s:stopped)EXPECT_TRUE(s.pixels.empty());
    EXPECT_THROW(pfcore::sampleSceneViews(batch,std::vector<pfcore::SceneViewRequest>{{2,0,1}}),std::invalid_argument);
}

TEST(VideoDecoder, ThreadedConversionPreservesSerialPixelsPtsAndDisplayGeometry)
{
    QTemporaryDir directory;
    const auto path=h264Fixture(directory,{"-bf","3"});
    if(path.isEmpty())GTEST_SKIP()<<"FFmpeg H264 unavailable";
    for(const auto [width,height]:{std::pair{127,73},std::pair{160,96},std::pair{1920,1080}})
        for(const int threads:{2,4,8}) {
            pfcore::VideoDecoder serial,parallel;
            pfcore::VideoDecodeOptions options;options.threads=2;options.maxWidth=width;options.maxHeight=height;
            serial.open(path.toStdString(),options);options.conversionThreads=threads;parallel.open(path.toStdString(),options);
            for(int i=0;i<40;++i) {
                pfcore::DecodedFrame a,b;
                ASSERT_TRUE(serial.readNext(a));ASSERT_TRUE(parallel.readNext(b));
                ASSERT_EQ(a.timestampSeconds,b.timestampSeconds);ASSERT_EQ(a.width,b.width);ASSERT_EQ(a.height,b.height);
                ASSERT_EQ(a.rgba,b.rgba)<<"threads="<<threads<<" frame="<<i;
            }
            serial.seek(1.25);parallel.seek(1.25);
            pfcore::VideoFrameSnapshot snapshot;ASSERT_TRUE(parallel.readNextSnapshot(snapshot));
            pfcore::DecodedFrame a,b;ASSERT_TRUE(serial.readNext(a));ASSERT_TRUE(parallel.convertSnapshotToRgba(snapshot,b));
            EXPECT_EQ(a.timestampSeconds,b.timestampSeconds);EXPECT_EQ(a.rgba,b.rgba);
        }
}

TEST(VideoDecoder, RealTenBitThreadedConversionPreservesSerialPixelsAndPts)
{
    const auto path=qEnvironmentVariable("PF_TEST_10BIT_SOURCE");
    if(path.isEmpty())GTEST_SKIP()<<"Opt-in real ten-bit source";
    for(const bool nvidia:{false,true})for(const int threads:{0,2,4,8}) {
        if(nvidia && qEnvironmentVariable("PF_TEST_RGBA_CPU_ONLY")=="1")continue;
        pfcore::VideoDecodeOptions options;options.threads=2;options.preferNvidia=nvidia;
        options.maxWidth=1280;options.maxHeight=720;
        pfcore::VideoDecoder serial,parallel;serial.open(path.toStdString(),options);
        options.conversionThreads=threads ? threads:1;options.autoNvidiaConversion=threads==0;
        parallel.open(path.toStdString(),options);
        if(nvidia && qEnvironmentVariable("PF_TEST_REQUIRE_NVDEC")=="1") {
            ASSERT_EQ(serial.diagnostics().backend,"nvdec");
            ASSERT_EQ(parallel.diagnostics().backend,"nvdec");
            if(threads==0 && std::uint64_t(parallel.info().width)*std::uint64_t(parallel.info().height)>=3840ull*1440
                && std::thread::hardware_concurrency()>=8) {
                EXPECT_EQ(parallel.diagnostics().conversionThreads,4);
            }
        }
        for(const double time:{20.,100.,900.}) {
            serial.seek(time);parallel.seek(time);
            for(int frame=0;frame<5;++frame) {
                pfcore::DecodedFrame a,b;ASSERT_TRUE(serial.readNext(a));ASSERT_TRUE(parallel.readNext(b));
                ASSERT_EQ(a.timestampSeconds,b.timestampSeconds);ASSERT_EQ(a.rgba,b.rgba)
                    <<"threads="<<threads<<" nvidia="<<nvidia<<" time="<<time;
            }
        }
    }
}

TEST(VideoDecoder, RealCinematicFourKAutoConversionKeepsPixelsAndReportsTimings)
{
    const auto path=qEnvironmentVariable("PF_TEST_10BIT_SOURCE");
    if(path.isEmpty())GTEST_SKIP()<<"Opt-in real cinematic 4K source";
    pfcore::VideoDecodeOptions options;options.threads=2;options.preferNvidia=true;
    options.maxWidth=1280;options.maxHeight=720;
    pfcore::VideoDecoder serial,parallel;serial.open(path.toStdString(),options);
    options.autoNvidiaConversion=true;parallel.open(path.toStdString(),options);
    if(serial.diagnostics().backend!="nvdec" || parallel.diagnostics().backend!="nvdec") {
        if(qEnvironmentVariable("PF_TEST_REQUIRE_NVDEC")=="1")FAIL()<<parallel.diagnostics().fallbackReason;
        GTEST_SKIP()<<"NVDEC unavailable";
    }
    const auto pixels=std::uint64_t(parallel.info().width)*std::uint64_t(parallel.info().height);
    const auto expectedThreads=pixels>=3840ull*1440 && std::thread::hardware_concurrency()>=8 ? 4 : 1;
    ASSERT_EQ(parallel.diagnostics().conversionThreads,expectedThreads);
    ASSERT_EQ(serial.diagnostics().conversionThreads,1);
    const auto target=std::min(20.0,serial.info().durationSeconds/4);
    serial.seek(target);parallel.seek(target);
    const auto beforeSerial=serial.diagnostics(),beforeParallel=parallel.diagnostics();
    int frames=0;
    for(;frames<120;++frames) {
        pfcore::DecodedFrame a,b;
        const bool haveSerial=serial.readNext(a),haveParallel=parallel.readNext(b);
        ASSERT_EQ(haveSerial,haveParallel);
        if(!haveSerial)break;
        ASSERT_DOUBLE_EQ(a.timestampSeconds,b.timestampSeconds)<<frames;
        ASSERT_EQ(a.width,b.width);ASSERT_EQ(a.height,b.height);
        ASSERT_EQ(a.rgba,b.rgba)<<"frame="<<frames;
    }
    ASSERT_GT(frames,0);
    const auto afterSerial=serial.diagnostics(),afterParallel=parallel.diagnostics();
    std::cout<<"cinematic4k frames="<<frames<<" source="<<parallel.info().width<<"x"<<parallel.info().height
        <<" serial_conversion_ms="<<afterSerial.conversionMilliseconds-beforeSerial.conversionMilliseconds
        <<" auto_conversion_ms="<<afterParallel.conversionMilliseconds-beforeParallel.conversionMilliseconds
        <<" auto_threads="<<afterParallel.conversionThreads<<'\n';
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
    EXPECT_GE(afterSeek.timestampSeconds, 0.5 - 1e-6);
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
                if(a.rgba!=b.rgba) {
                    std::size_t changed=0;int maximum=0;
                    for(std::size_t i=0;i<a.rgba.size();++i) {
                        const int delta=std::abs(int(a.rgba[i])-int(b.rgba[i]));
                        changed+=delta!=0;maximum=std::max(maximum,delta);
                    }
                    FAIL()<<path.toStdString()<<" at "<<a.timestampSeconds
                        <<" changed bytes="<<changed<<" max delta="<<maximum;
                }
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

TEST(VideoDecoder, TenBitNvidiaDecodeAndRandomSeeksPreserveCpuPixels)
{
    if (!qEnvironmentVariableIsSet("PF_TEST_REQUIRE_NVDEC"))
        GTEST_SKIP() << "Opt-in NVIDIA 10-bit integration";
    QTemporaryDir directory;
    const auto path = directory.filePath("ten-bit.mp4");
    QProcess generator;
    generator.start(QStandardPaths::findExecutable("ffmpeg"), {"-hide_banner", "-loglevel", "error",
        "-y", "-f", "lavfi", "-i", "testsrc2=size=384x216:rate=24:duration=2",
        "-pix_fmt", "yuv420p10le", "-c:v", "libx265", "-preset", "ultrafast",
        "-x265-params", "pools=2:frame-threads=2:log-level=error:keyint=24", path});
    ASSERT_TRUE(generator.waitForFinished(30000));
    ASSERT_EQ(generator.exitCode(), 0);
    pfcore::VideoDecodeOptions options;
    options.maxWidth = 160; options.maxHeight = 90;
    pfcore::VideoDecoder cpu, gpu;
    cpu.open(path.toStdString(), options);
    options.preferNvidia = true;
    gpu.open(path.toStdString(), options);
    ASSERT_EQ(gpu.diagnostics().backend, "nvdec") << gpu.diagnostics().fallbackReason;
    pfcore::DecodedFrame a, b;
    std::size_t count = 0;
    while (cpu.readNext(a)) {
        ASSERT_TRUE(gpu.readNext(b));
        EXPECT_EQ(a.timestampSeconds, b.timestampSeconds);
        EXPECT_EQ(a.rgba, b.rgba) << "frame=" << count;
        ++count;
    }
    EXPECT_EQ(count, 48U); EXPECT_FALSE(gpu.readNext(b));
    for (const double time : {1.2, .25, 1.65, .6}) {
        cpu.seek(time); gpu.seek(time);
        do { ASSERT_TRUE(cpu.readNext(a)); } while(a.timestampSeconds + 1e-6 < time);
        do { ASSERT_TRUE(gpu.readNext(b)); } while(b.timestampSeconds + 1e-6 < time);
        EXPECT_EQ(a.timestampSeconds, b.timestampSeconds);
        EXPECT_EQ(a.rgba, b.rgba);
    }
}

TEST(VideoDecoder, RetainedFramesPreservePixelsAcrossLaterReadsAndSeeksButRejectAnotherSource)
{
    QTemporaryDir directory; const auto path = h264Fixture(directory, {"-bf", "3"});
    if (path.isEmpty()) GTEST_SKIP() << "FFmpeg H264 unavailable";
    pfcore::VideoDecoder decoder, reference;
    decoder.open(path.toStdString()); reference.open(path.toStdString());
    decoder.setRgbaMaxDimensions(160, 90); reference.setRgbaMaxDimensions(160, 90);
    std::vector<pfcore::VideoFrameSnapshot> frames;
    std::vector<pfcore::DecodedFrame> expected;
    for (int i = 0; i < 24; ++i) {
        pfcore::VideoFrameSnapshot snapshot; ASSERT_TRUE(decoder.readNextSnapshot(snapshot));
        frames.push_back(std::move(snapshot));
        pfcore::DecodedFrame rgba; ASSERT_TRUE(reference.readNext(rgba)); expected.push_back(std::move(rgba));
    }
    decoder.seek(1.5);
    for (int i = 23; i >= 0; --i) {
        pfcore::DecodedFrame rgba; ASSERT_TRUE(decoder.convertSnapshotToRgba(frames[i], rgba));
        EXPECT_EQ(rgba.timestampSeconds, expected[i].timestampSeconds); EXPECT_EQ(rgba.rgba, expected[i].rgba);
    }
    pfcore::DecodedFrame ignored;
    EXPECT_FALSE(decoder.convertSnapshotToRgba({}, ignored));
    EXPECT_THROW(reference.convertSnapshotToRgba(frames.front(), ignored), std::invalid_argument);
    decoder.close(); EXPECT_THROW(decoder.convertSnapshotToRgba(frames.front(), ignored), std::invalid_argument);
    decoder.open(path.toStdString());
    EXPECT_THROW(decoder.convertSnapshotToRgba(frames.front(), ignored), std::invalid_argument);
}

TEST(VideoDecoder, RealTenBitRetainedNvidiaFramesPreserveCpuPixelsAfterSeek)
{
    const auto path = qEnvironmentVariable("PF_TEST_10BIT_SOURCE");
    if (path.isEmpty()) GTEST_SKIP() << "Opt-in real 10-bit retained NVIDIA frames";
    pfcore::VideoDecodeOptions options; options.maxWidth = 1280; options.maxHeight = 720;
    pfcore::VideoDecoder cpu, gpu; cpu.open(path.toStdString(), options);
    options.preferNvidia = true; gpu.open(path.toStdString(), options);
    ASSERT_EQ(gpu.diagnostics().backend, "nvdec");
    for (const double fraction : {0., .2, .7, .95}) {
        const double target = cpu.info().durationSeconds * fraction;
        cpu.seek(target); gpu.seek(target);
        pfcore::DecodedFrame rgba; pfcore::VideoFrameSnapshot snapshot;
        do { ASSERT_TRUE(cpu.readNext(rgba)); } while (rgba.timestampSeconds + 1e-6 < target);
        do { ASSERT_TRUE(gpu.readNextSnapshot(snapshot)); } while (snapshot.timestampSeconds + 1e-6 < target);
        std::vector<pfcore::DecodedFrame> expected{rgba};
        std::vector<pfcore::VideoFrameSnapshot> retained{snapshot};
        for (int i = 0; i < 3; ++i) {
            ASSERT_TRUE(cpu.readNext(rgba)); expected.push_back(rgba);
            ASSERT_TRUE(gpu.readNextSnapshot(snapshot)); retained.push_back(snapshot);
        }
        gpu.seek(target + 2);
        for (int i = 3; i >= 0; --i) {
            ASSERT_TRUE(gpu.convertSnapshotToRgba(retained[i], rgba));
            EXPECT_EQ(rgba.timestampSeconds, expected[i].timestampSeconds); EXPECT_EQ(rgba.rgba, expected[i].rgba);
        }
    }
}

TEST(VideoDecoder, RealTenBitNvidiaSeeksPreserveCpuPixels)
{
    const auto path=qEnvironmentVariable("PF_TEST_10BIT_SOURCE");
    if(path.isEmpty())GTEST_SKIP()<<"Opt-in real 10-bit NVIDIA source";
    pfcore::VideoDecodeOptions options; options.maxWidth=1280; options.maxHeight=720;
    pfcore::VideoDecoder cpu,gpu; cpu.open(path.toStdString(),options);
    options.preferNvidia=true; gpu.open(path.toStdString(),options);
    ASSERT_EQ(gpu.diagnostics().backend,"nvdec")<<gpu.diagnostics().fallbackReason;
    pfcore::DecodedFrame a,b;
    for(const auto fraction:{0.,.05,.20,.45,.70,.95}) {
        const auto target=fraction*cpu.info().durationSeconds;
        cpu.seek(target);gpu.seek(target);
        do {ASSERT_TRUE(cpu.readNext(a,false));}while(a.timestampSeconds+1e-6<target);
        do {ASSERT_TRUE(gpu.readNext(b,false));}while(b.timestampSeconds+1e-6<target);
        for(int sample=0;sample<12;++sample) {
            ASSERT_EQ(a.timestampSeconds,b.timestampSeconds);
            ASSERT_TRUE(cpu.convertCurrentFrameToRgba(a));
            ASSERT_TRUE(gpu.convertCurrentFrameToRgba(b));
            ASSERT_EQ(a.rgba,b.rgba)<<"target="<<target<<" sample="<<sample;
            ASSERT_TRUE(cpu.readNext(a,false));ASSERT_TRUE(gpu.readNext(b,false));
        }
    }
    EXPECT_GT(gpu.diagnostics().reusedDownloadBuffers, 60U);
}

TEST(VideoDecoder, RealTenBitNvidiaPreviewsPreserveFullHdPixelsAndPts)
{
    const auto path=qEnvironmentVariable("PF_TEST_10BIT_SOURCE");
    if(path.isEmpty())GTEST_SKIP()<<"Opt-in real 10-bit NVIDIA preview source";
    pfcore::VideoDecodeOptions options;options.maxWidth=1920;options.maxHeight=1080;
    pfcore::VideoDecoder cpu,gpu;cpu.open(path.toStdString(),options);
    options.preferNvidia=true;gpu.open(path.toStdString(),options);
    ASSERT_EQ(gpu.diagnostics().backend,"nvdec")<<gpu.diagnostics().fallbackReason;
    pfcore::DecodedFrame a,b;
    // Reversed seeks reproduce the nonchronological result preview ordering.
    for(const auto fraction:{.95,.70,.45,.20,.05,0.}) {
        const auto target=fraction*cpu.info().durationSeconds;
        cpu.seek(target);gpu.seek(target);
        do {ASSERT_TRUE(cpu.readNext(a,false));}while(a.timestampSeconds+1e-3<target);
        do {ASSERT_TRUE(gpu.readNext(b,false));}while(b.timestampSeconds+1e-3<target);
        ASSERT_EQ(a.timestampSeconds,b.timestampSeconds);
        ASSERT_TRUE(cpu.convertCurrentFrameToRgba(a));
        ASSERT_TRUE(gpu.convertCurrentFrameToRgba(b));
        ASSERT_EQ(a.width,1920);ASSERT_EQ(a.height,1080);
        ASSERT_EQ(a.rgba,b.rgba)<<"preview target="<<target;
    }
}

TEST(VideoDecoder, RealPreviewOrderingPreservesFramesAndReportsDecodeWork)
{
    const auto reportPath=qEnvironmentVariable("PF_PREVIEW_ORDER_REPORT");
    if(reportPath.isEmpty())GTEST_SKIP()<<"Opt-in real-source preview order benchmark";
    QFile report(reportPath);ASSERT_TRUE(report.open(QIODevice::ReadOnly));
    const auto document=QJsonDocument::fromJson(report.readAll());ASSERT_TRUE(document.isObject());
    struct Request {std::string source;double seconds=0;};
    std::vector<Request> requests;
    std::unordered_set<std::string> seen;
    for(const auto value:document.object()["results"].toArray()) {
        const auto row=value.toObject();
        for(const auto& side:{QStringLiteral("left"),QStringLiteral("right")}) {
            const auto source=row[side+"Source"].toString().toStdString();
            const double seconds=std::max(0.,row[side+"Start"].toDouble(-1));
            if(source.empty() || !std::isfinite(seconds))continue;
            const auto micros=std::llround(seconds*1'000'000.);
            const auto key=source+"\n"+std::to_string(micros);
            if(seen.insert(key).second)requests.push_back({source,micros/1'000'000.});
        }
    }
    ASSERT_GT(requests.size(),1U);
    const int requestedLimit=qEnvironmentVariableIntValue("PF_PREVIEW_ORDER_LIMIT");
    if(requestedLimit>1 && requests.size()>static_cast<std::size_t>(requestedLimit)) {
        std::vector<Request> sample;sample.reserve(requestedLimit);
        for(int i=0;i<requestedLimit;++i) {
            const auto index=static_cast<std::size_t>(i)*(requests.size()-1)/(requestedLimit-1);
            sample.push_back(requests[index]);
        }
        requests=std::move(sample);
    }
    std::vector<pfui::PreviewRequestOrderKey> keys;keys.reserve(requests.size());
    for(const auto& request:requests)keys.push_back({request.source,request.seconds});
    const auto sorted=pfui::previewRenderOrder(keys);
    std::vector<std::size_t> original(requests.size());
    std::iota(original.begin(),original.end(),std::size_t{0});
    struct Pass {std::vector<QByteArray> hashes;std::vector<double> times;std::set<std::string> backends;std::uint64_t decoded=0;double elapsedMs=0;};
    const auto run=[&](const std::vector<std::size_t>& order) {
        Pass pass;pass.hashes.resize(requests.size());pass.times.resize(requests.size());
        std::map<std::string,std::unique_ptr<pfcore::VideoDecoder>> decoders;
        std::list<std::string> lru;
        const auto collect=[&](const std::string& source) {
            const auto found=decoders.find(source);
            if(found!=decoders.end())pass.decoded+=found->second->diagnostics().decodedFrames;
        };
        QElapsedTimer timer;timer.start();
        for(const auto index:order) {
            const auto& request=requests[index];
            auto found=decoders.find(request.source);
            if(found==decoders.end()) {
                if(decoders.size()>=2) {
                    const auto evicted=lru.front();lru.pop_front();collect(evicted);decoders.erase(evicted);
                }
                auto decoder=std::make_unique<pfcore::VideoDecoder>();
                pfcore::VideoDecodeOptions options;options.maxWidth=1920;options.maxHeight=1080;
                options.minimumNvidiaPixels=1920ULL*1080ULL+1;options.preferNvidia=true;
                decoder->open(request.source,options);decoder->setRgbaMaxDimensions(1920,1080);
                pass.backends.insert(decoder->diagnostics().backend);
                found=decoders.emplace(request.source,std::move(decoder)).first;
            } else lru.remove(request.source);
            lru.push_back(request.source);
            try {
                auto& decoder=*found->second;decoder.seek(request.seconds);
                pfcore::DecodedFrame frame;bool available=false;
                while((available=decoder.readNext(frame,false)) && frame.timestampSeconds+1e-3<request.seconds) {}
                if(!available || !decoder.convertCurrentFrameToRgba(frame)) {
                    ADD_FAILURE()<<"Could not decode preview "<<request.source<<" @ "<<request.seconds;
                    break;
                }
                pass.times[index]=frame.timestampSeconds;
                pass.hashes[index]=QCryptographicHash::hash(
                    QByteArray(reinterpret_cast<const char*>(frame.rgba.data()),static_cast<qsizetype>(frame.rgba.size())),
                    QCryptographicHash::Sha256);
            } catch(const std::exception& error) {
                ADD_FAILURE()<<"Preview decode failed: "<<error.what();break;
            }
        }
        for(const auto& source:lru)collect(source);
        pass.elapsedMs=timer.elapsed();
        return pass;
    };
    std::vector<Pass> baselinePasses,optimizedPasses;
    for(int repeat=0;repeat<3;++repeat) {
        if(repeat%2==0) {baselinePasses.push_back(run(original));optimizedPasses.push_back(run(sorted));}
        else {optimizedPasses.push_back(run(sorted));baselinePasses.push_back(run(original));}
    }
    const auto& reference=baselinePasses.front();
    std::vector<double> baselineTimes,optimizedTimes;
    for(const auto& pass:baselinePasses)baselineTimes.push_back(pass.elapsedMs);
    for(const auto& pass:optimizedPasses)optimizedTimes.push_back(pass.elapsedMs);
    std::sort(baselineTimes.begin(),baselineTimes.end());
    std::sort(optimizedTimes.begin(),optimizedTimes.end());
    ASSERT_EQ(reference.hashes.size(),requests.size());
    for(std::size_t i=0;i<requests.size();++i) {
        ASSERT_FALSE(reference.hashes[i].isEmpty())<<i;
        for(const auto& pass:baselinePasses) {
            EXPECT_EQ(pass.hashes[i],reference.hashes[i])<<"baseline iteration="<<i;
            EXPECT_DOUBLE_EQ(pass.times[i],reference.times[i]);
        }
        for(const auto& pass:optimizedPasses) {
            EXPECT_EQ(pass.hashes[i],reference.hashes[i])<<requests[i].source<<" @ "<<requests[i].seconds;
            EXPECT_DOUBLE_EQ(pass.times[i],reference.times[i]);
        }
    }
    for(const auto& pass:baselinePasses)EXPECT_EQ(pass.decoded,reference.decoded);
    for(const auto& pass:optimizedPasses)EXPECT_EQ(pass.decoded,reference.decoded);
    std::cout<<"PF_PREVIEW_ORDER_BENCH samples="<<requests.size()<<" repeats=3"
        <<" original_median_ms="<<baselineTimes[1]<<" sorted_median_ms="<<optimizedTimes[1]
        <<" original_decoded_frames="<<reference.decoded
        <<" sorted_decoded_frames="<<optimizedPasses.front().decoded<<" backend=";
    for(const auto& backend:optimizedPasses.front().backends)std::cout<<backend<<',';
    std::cout<<'\n';
}

} // namespace

TEST(VideoDecoder, ParallelSceneViewsMatchSerialPixelsAndKeepCallbacksOnCaller)
{
    QTemporaryDir directory;const auto path=h264Fixture(directory,{"-bf","3"});
    if(path.isEmpty())GTEST_SKIP()<<"FFmpeg H264 unavailable";
    std::vector<pfcore::SceneViewRequest> requests;
    for(int i=0;i<96;++i)requests.push_back({(95-i)*.018,0,2});
    pfcore::VideoDecoder decoder;decoder.open(path.toStdString());decoder.setRgbaMaxDimensions(160,90);
    const auto serial=pfcore::sampleSceneViews(decoder,requests);
    const auto caller=std::this_thread::get_id();std::size_t previous=0;
    const auto parallel=pfcore::sampleSceneViewsParallel(path.toStdString(),requests,{},[&](auto done,auto total) {
        EXPECT_EQ(std::this_thread::get_id(),caller);EXPECT_GE(done,previous);EXPECT_LE(done,total);previous=done;
    },6);
    ASSERT_EQ(serial.size(),parallel.size());EXPECT_EQ(previous,requests.size());
    for(std::size_t i=0;i<serial.size();++i) {
        EXPECT_DOUBLE_EQ(serial[i].timestampSeconds,parallel[i].timestampSeconds);
        EXPECT_EQ(serial[i].pixels,parallel[i].pixels)<<i;
    }
    bool stop=false;
    const auto cancelled=pfcore::sampleSceneViewsParallel(path.toStdString(),requests,[&] {return stop;},
        [&](auto done,auto) {if(done==0)stop=true;},8);
    for(const auto& s:cancelled)EXPECT_TRUE(s.pixels.empty());
    EXPECT_THROW(pfcore::sampleSceneViewsParallel(path.toStdString(),requests,{}, {},0),std::invalid_argument);
    EXPECT_THROW(pfcore::sampleSceneViewsParallel("missing-source.mp4",requests),std::runtime_error);
}

TEST(VideoDecoder, RealTenBitParallelNvidiaSceneViewsPreserveCpuPixelsAndPts)
{
    const auto path=qEnvironmentVariable("PF_TEST_10BIT_SOURCE");
    if(path.isEmpty())GTEST_SKIP()<<"Opt-in real 10-bit NVIDIA source";
    pfcore::VideoDecoder cpu,gpu;
    cpu.open(path.toStdString());cpu.setRgbaMaxDimensions(160,90);
    pfcore::VideoDecodeOptions options;options.preferNvidia=true;
    gpu.open(path.toStdString(),options);
    ASSERT_EQ(gpu.diagnostics().backend,"nvdec")<<gpu.diagnostics().fallbackReason;
    std::vector<pfcore::SceneViewRequest> requests;
    const auto duration=cpu.info().durationSeconds;
    for(int i=0;i<32;++i)requests.push_back({duration*(31-i+.5)/32,0,duration});
    const auto expected=pfcore::sampleSceneViews(cpu,requests);
    const auto caller=std::this_thread::get_id();std::size_t previous=0;
    const auto actual=pfcore::sampleSceneViewsParallel(path.toStdString(),requests,{},
        [&](auto done,auto total) {
            EXPECT_EQ(std::this_thread::get_id(),caller);
            EXPECT_GE(done,previous);EXPECT_LE(done,total);previous=done;
        },8,true);
    ASSERT_EQ(expected.size(),actual.size());
    EXPECT_EQ(previous,requests.size());
    EXPECT_GT(std::count_if(expected.begin(),expected.end(),
        [](const auto& observation){return !observation.pixels.empty();}),16);
    for(std::size_t i=0;i<expected.size();++i) {
        EXPECT_DOUBLE_EQ(expected[i].timestampSeconds,actual[i].timestampSeconds)<<i;
        EXPECT_EQ(expected[i].pixels,actual[i].pixels)<<i;
    }
}

TEST(VideoDecoder, LongGopViewsAvoidRepeatedSeeksWithoutChangingPixels)
{
    QTemporaryDir directory;const auto path=h264Fixture(directory,{"-g","240","-bf","3"},12);
    if(path.isEmpty())GTEST_SKIP()<<"FFmpeg H264 unavailable";
    pfcore::VideoDecoder batch,reference;batch.open(path.toStdString());reference.open(path.toStdString());
    batch.setRgbaMaxDimensions(160,90);reference.setRgbaMaxDimensions(160,90);
    const std::vector<pfcore::SceneViewRequest> requests={{1,0,12},{4,0,12},{7,0,12},{10,0,12}};
    const auto actual=pfcore::sampleSceneViews(batch,requests);
    for(std::size_t i=0;i<requests.size();++i) {
        reference.seek(requests[i].target);pfcore::DecodedFrame frame;std::vector<float> expected;
        while(reference.readNext(frame,false)) {
            if(frame.timestampSeconds+1e-6<requests[i].target)continue;
            ASSERT_TRUE(reference.convertCurrentFrameToRgba(frame));
            expected=pfcore::sceneViewDescriptor({frame.timestampSeconds,frame.width,frame.height,frame.rgba});break;
        }
        EXPECT_EQ(actual[i].pixels,expected)<<i;
    }
    EXPECT_LT(batch.diagnostics().decodedFrames,reference.diagnostics().decodedFrames);
    EXPECT_LT(batch.diagnostics().decodedFrames,400U);
}

TEST(VideoSampleReader, SampleClockBoundsDiscontinuousAndUnrepresentableTimestamps) {
    double next = 0;
    EXPECT_TRUE(pfcore::detail::selectSampleTimestamp(1e12, 1.0 / 24.0, next));
    EXPECT_GT(next, 1e12);
    next = 1e20;
    EXPECT_TRUE(pfcore::detail::selectSampleTimestamp(1e20, 1.0 / 24.0, next));
    EXPECT_GT(next, 1e20);
    EXPECT_THROW(pfcore::detail::selectSampleTimestamp(std::numeric_limits<double>::quiet_NaN(), 1, next), std::invalid_argument);
}
TEST(VideoSampleReader, SampleClockPreservesOrdinaryRepeatedAdditionCadence) {
    for (const auto fps : {4.0, 6.0, 10.0, 15.0, 24.0}) {
        double expected = -std::numeric_limits<double>::infinity(), actual = expected;
        for (int frame = 0; frame < 10000; ++frame) {
            const double timestamp = frame / 60.0;
            bool selected = timestamp + 1e-9 >= expected;
            if (selected) {
                if (!std::isfinite(expected)) expected = timestamp + 1.0 / fps;
                else do { expected += 1.0 / fps; } while (expected <= timestamp + 1e-9);
            }
            EXPECT_EQ(pfcore::detail::selectSampleTimestamp(timestamp, 1.0 / fps, actual), selected);
            EXPECT_EQ(actual, expected);
        }
    }
}
#include <pfcore/detail/PixelPlanes.hpp>

TEST(PixelPlanes, P010LumaPreservesEveryInputWordIncludingUnalignedTail)
{
    std::vector<std::uint8_t> source(65536 * 2 + 2, 0xa5), actual(source.size(), 0xa5), expected=actual;
    for (unsigned word=0; word<65536; ++word) {
        source[1+word*2]=static_cast<std::uint8_t>(word);
        source[2+word*2]=static_cast<std::uint8_t>(word>>8);
        expected[1+word*2]=static_cast<std::uint8_t>(word>>6);
        expected[2+word*2]=static_cast<std::uint8_t>(word>>14);
    }
    pfcore::detail::unpackP010Luma(source.data()+1,actual.data()+1,65536);
    EXPECT_EQ(actual,expected);
}

TEST(PixelPlanes, ChromaPreservesOddWidthsUnalignedRowsAndGuardBytes)
{
    for (std::size_t count=0;count<67;++count) {
        std::vector<std::uint8_t> source(count*4+2,0xa5);
        for(std::size_t i=1;i+1<source.size();++i)source[i]=static_cast<std::uint8_t>(i*37+count*11);
        std::vector<std::uint8_t> u(count*2+2,0xa5),v=u,expectedU=u,expectedV=v;
        for(std::size_t x=0;x<count;++x) {
            const unsigned a=source[1+x*4] | (unsigned(source[2+x*4])<<8);
            const unsigned b=source[3+x*4] | (unsigned(source[4+x*4])<<8);
            expectedU[1+x*2]=static_cast<std::uint8_t>(a>>6);expectedU[2+x*2]=static_cast<std::uint8_t>(a>>14);
            expectedV[1+x*2]=static_cast<std::uint8_t>(b>>6);expectedV[2+x*2]=static_cast<std::uint8_t>(b>>14);
        }
        pfcore::detail::unpackP010Chroma(source.data()+1,u.data()+1,v.data()+1,count);
        EXPECT_EQ(u,expectedU)<<count;EXPECT_EQ(v,expectedV)<<count;
        u.assign(count+2,0xa5);v=u;expectedU=u;expectedV=v;
        for(std::size_t x=0;x<count;++x) {expectedU[1+x]=source[1+x*2];expectedV[1+x]=source[2+x*2];}
        pfcore::detail::unpackNv12Chroma(source.data()+1,u.data()+1,v.data()+1,count);
        EXPECT_EQ(u,expectedU)<<count;EXPECT_EQ(v,expectedV)<<count;
    }
}
