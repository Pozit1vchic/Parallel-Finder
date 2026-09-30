#include <gtest/gtest.h>

#include <pfservices/CutService.hpp>
#include <pfservices/ExportQueue.hpp>
#include <pfservices/FfmpegCapabilities.hpp>
#include <pfservices/ProviderManager.hpp>
#include <pfservices/PreviewMemo.hpp>
#include <pfcore/VideoDecoder.hpp>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include <filesystem>
#include <fstream>
#include <thread>
#include <atomic>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <QFileInfo>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>

TEST(PreviewMemo, RendersEachSourceTimestampOnlyOnce)
{
    int calls = 0;
    pfservices::PreviewMemo memo([&](const QString&, double) {
        return QString("file:///preview_%1.png").arg(++calls);
    });
    const auto first = memo.get("a", 42.5);
    EXPECT_EQ(memo.get("a", 42.5), first);
    EXPECT_NE(memo.get("b", 42.5), first);
    EXPECT_NE(memo.get("a", 42.6), first);
    EXPECT_EQ(calls, 3);
}

TEST(PreviewMemo, FailedRenderingCanBeRetried)
{
    int calls = 0;
    pfservices::PreviewMemo memo([&](const QString&, double) {
        return ++calls == 1 ? QString{} : QString("file:///preview.png");
    });
    EXPECT_TRUE(memo.get("a", 0).isEmpty());
    EXPECT_EQ(memo.get("a", 0), "file:///preview.png");
    EXPECT_EQ(memo.get("a", 0), "file:///preview.png");
    EXPECT_EQ(calls, 2);
}

TEST(CutService, RealVideoPreservesResolutionFrameRateAndAudio)
{
    const auto source = qEnvironmentVariable("PF_TEST_EXPORT_SOURCE");
    if (source.isEmpty()) GTEST_SKIP() << "Opt-in real-video export integration";
    const auto ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    const auto ffprobe = QStandardPaths::findExecutable("ffprobe");
    ASSERT_FALSE(ffmpeg.isEmpty());
    ASSERT_FALSE(ffprobe.isEmpty());
    const auto inspect = [&](const QString& path) {
        QProcess probe;
        probe.start(ffprobe, {"-v", "error", "-show_entries",
            "stream=codec_type,width,height,r_frame_rate:format=duration", "-of", "json", path});
        if (!probe.waitForFinished(30000) || probe.exitCode() != 0) return QJsonObject{};
        return QJsonDocument::fromJson(probe.readAllStandardOutput()).object();
    };
    const auto original = inspect(source);
    ASSERT_FALSE(original.isEmpty());
    const auto streams = original.value("streams").toArray();
    ASSERT_FALSE(streams.isEmpty());
    const auto sourceVideo = streams.first().toObject();
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const pfservices::CutService cutter(ffmpeg.toStdString());
    for (const auto mode : {pfservices::CutMode::Exact, pfservices::CutMode::Fast}) {
        pfservices::CutRequest request;
        request.inputPath = source.toStdWString();
        const auto output = directory.filePath(mode == pfservices::CutMode::Exact ? "exact.mp4" : "fast.mp4");
        request.outputPath = output.toStdWString();
        request.startSeconds = 60;
        request.endSeconds = 87;
        request.mode = mode;
        const auto cut = cutter.cut(request);
        ASSERT_TRUE(cut.success) << cut.error;
        const auto metadata = inspect(output);
        const auto exportedStreams = metadata.value("streams").toArray();
        ASSERT_EQ(exportedStreams.size(), streams.size());
        const auto video = exportedStreams.first().toObject();
        EXPECT_EQ(video.value("width"), sourceVideo.value("width"));
        EXPECT_EQ(video.value("height"), sourceVideo.value("height"));
        EXPECT_EQ(video.value("r_frame_rate"), sourceVideo.value("r_frame_rate"));
        bool audio = false;
        for (const auto& stream : exportedStreams)
            audio = audio || stream.toObject().value("codec_type").toString() == "audio";
        EXPECT_TRUE(audio);
        EXPECT_GE(metadata.value("format").toObject().value("duration").toString().toDouble(), 26.9);
    }
}

TEST(CutService, ExistingDestinationSurvivesFailedReplacement)
{
    const auto ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    if (ffmpeg.isEmpty()) GTEST_SKIP() << "FFmpeg unavailable";
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    const auto source = temporary.filePath("source.mp4");
    QProcess generator;
    generator.start(ffmpeg, {"-hide_banner", "-loglevel", "error", "-f", "lavfi",
        "-i", "color=c=blue:s=160x90:r=15", "-t", "2", "-c:v", "mpeg4",
        "-y", source});
    ASSERT_TRUE(generator.waitForFinished(30000));
    ASSERT_EQ(generator.exitCode(), 0);
    const auto destination = std::filesystem::path(temporary.filePath("cut.mp4").toStdWString());
    pfservices::CutRequest request;
    request.inputPath = source.toStdWString();
    request.outputPath = destination;
    request.startSeconds = 0;
    request.endSeconds = 1;
    request.mode = pfservices::CutMode::Fast;
    pfservices::CutService cutter(ffmpeg.toStdString());
    { std::ofstream previous(destination); previous << "previous"; }
    const auto replaced = cutter.cut(request);
    ASSERT_TRUE(replaced.success) << replaced.error;
    EXPECT_GT(std::filesystem::file_size(destination), 100U);
    std::filesystem::remove(destination);
    std::filesystem::create_directory(destination);
    { std::ofstream sentinel(destination / "keep.txt"); sentinel << "keep"; }
    const auto rejected = cutter.cut(request);
    EXPECT_FALSE(rejected.success);
    EXPECT_TRUE(std::filesystem::exists(destination / "keep.txt"));
}

TEST(CutService, ExactLateSeekPreservesFirstFrameDurationAndReportsProgress)
{
    const auto ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    if (ffmpeg.isEmpty()) GTEST_SKIP() << "FFmpeg unavailable";
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto source = directory.filePath("long-gop.mp4");
    QProcess generator;
    generator.start(ffmpeg, {"-hide_banner", "-loglevel", "error", "-y",
        "-f", "lavfi", "-i", "testsrc2=size=160x90:rate=10:duration=12",
        "-c:v", "libx264", "-preset", "veryfast", "-crf", "18",
        "-g", "120", "-keyint_min", "120", "-sc_threshold", "0", source});
    ASSERT_TRUE(generator.waitForFinished(30000));
    ASSERT_EQ(generator.exitCode(), 0) << generator.readAllStandardError().toStdString();
    pfcore::VideoDecoder original;
    original.open(source.toStdString());
    original.seek(9.3);
    pfcore::DecodedFrame expected;
    while (original.readNext(expected, false)) {
        if (expected.timestampSeconds + 0.001 >= 9.3) {
            ASSERT_TRUE(original.convertCurrentFrameToRgba(expected));
            break;
        }
    }
    ASSERT_FALSE(expected.rgba.empty());
    pfservices::CutRequest request;
    request.inputPath = source.toStdWString();
    request.outputPath = directory.filePath("cut.mp4").toStdWString();
    request.startSeconds = 9.3;
    request.endSeconds = 10.3;
    int updates = 0;
    double lastProgress = -1;
    request.progress = [&](double seconds) {
        EXPECT_GE(seconds, 0.0);
        EXPECT_LE(seconds, 1.0);
        ++updates;
        lastProgress = seconds;
    };
    pfservices::CutService cutter(ffmpeg.toStdString());
    const auto cut = cutter.cut(request);
    ASSERT_TRUE(cut.success) << cut.error;
    const auto seek = std::find(cut.arguments.begin(), cut.arguments.end(), "-ss");
    const auto input = std::find(cut.arguments.begin(), cut.arguments.end(), "-i");
    EXPECT_LT(seek, input);
    EXPECT_GT(updates, 0);
    EXPECT_DOUBLE_EQ(lastProgress, 1.0);
    pfcore::VideoDecoder output;
    output.open(request.outputPath.string());
    pfcore::DecodedFrame actual;
    ASSERT_TRUE(output.readNext(actual));
    ASSERT_EQ(actual.rgba.size(), expected.rgba.size());
    double error = 0;
    for (std::size_t i = 0; i < actual.rgba.size(); ++i)
        error += std::abs(int(actual.rgba[i]) - int(expected.rgba[i]));
    EXPECT_LT(error / actual.rgba.size(), 12.0); // lossy encode, not the keyframe at zero
    int frames = 1;
    while (output.readNext(actual, false)) ++frames;
    EXPECT_EQ(frames, 10);
    request.outputPath = directory.filePath("second.mp4").toStdWString();
    const auto second = cutter.cut(request);
    ASSERT_TRUE(second.success) << second.error;
    EXPECT_EQ(second.encoder, cut.encoder);
    // No failed hardware retries before the cached working encoder.
    EXPECT_TRUE(second.error.empty());
}

TEST(CutService, StalledEncoderStopsAutomaticallyAndKeepsExistingDestination)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    pfservices::CutRequest request;
    request.inputPath = std::filesystem::path(PF_TEST_FIXTURE_DIR) / "tiny.mp4";
    request.outputPath = directory.filePath("cut.mp4").toStdWString();
    { std::ofstream previous(request.outputPath); previous << "keep"; }
    request.endSeconds = 1.0;
    request.stallTimeoutMs = 200;
    const pfservices::CutService cutter(PF_TEST_FFMPEG_STUB);
    const auto start = std::chrono::steady_clock::now();
    const auto result = cutter.cut(request);
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.cancelled);
    EXPECT_NE(result.error.find("stopped advancing"), std::string::npos);
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(5));
    EXPECT_EQ(std::filesystem::file_size(request.outputPath), 4u);
    EXPECT_FALSE(QFileInfo::exists(directory.filePath("cut.part.mp4")));
}

TEST(ProviderManager, KeepsIndependentInstallationsAndRejectsUnvalidatedRuntime)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    for (const auto* name : {"cuda", "dml", "tensorrt"}) {
        const QString path = directory.filePath(QString::fromLatin1(name));
        ASSERT_TRUE(QDir().mkpath(path));
        QFile library(path + "/onnxruntime.dll");
        ASSERT_TRUE(library.open(QIODevice::WriteOnly));
        library.write("invalid runtime");
    }
    const auto found = pfservices::ProviderManager::scan({directory.path()}, "nonexistent-probe-executable");
    ASSERT_EQ(found.size(), 3u);
    for (const auto& provider : found) {
        EXPECT_TRUE(provider.installed);
        EXPECT_FALSE(provider.available);
        EXPECT_FALSE(provider.reason.isEmpty());
    }
    ASSERT_TRUE(QFile::remove(directory.filePath("dml/onnxruntime.dll")));
    const auto refreshed = pfservices::ProviderManager::scan({directory.path()}, "nonexistent-probe-executable");
    ASSERT_EQ(refreshed.size(), 3u);
    for (const auto& provider : refreshed)
        EXPECT_EQ(provider.installed, provider.name != "dml");
}

TEST(ProviderManager, InstalledBundlesPassIsolatedSessionProbes)
{
    const auto root = qEnvironmentVariable("PF_TEST_PROVIDER_ROOT");
    const auto executable = qEnvironmentVariable("PF_TEST_PROBE_EXE");
    if (root.isEmpty() || executable.isEmpty()) GTEST_SKIP() << "Opt-in GPU bundle integration";
    const auto states = pfservices::ProviderManager::scan({root}, executable);
    ASSERT_EQ(states.size(), 3u);
    for (const auto& state : states) {
        EXPECT_TRUE(state.installed) << state.name.toStdString();
        EXPECT_TRUE(state.available) << state.name.toStdString() << ": " << state.reason.toStdString();
    }
}

TEST(ProviderManager, DiscoveryAndStartupShareUserFirstSelection)
{
    QTemporaryDir userRoot, appRoot;
    ASSERT_TRUE(userRoot.isValid());
    ASSERT_TRUE(appRoot.isValid());
    const auto userDirectory = userRoot.filePath("cuda/wrapper");
    const auto appDirectory = appRoot.filePath("cuda");
    for (const auto& path : {userDirectory, appDirectory}) {
        ASSERT_TRUE(QDir().mkpath(path));
        QFile file(path + "/onnxruntime.dll");
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("test");
    }
    EXPECT_EQ(pfservices::ProviderManager::locateRuntime({userRoot.path(), appRoot.path()}, "cuda"),
        userDirectory + "/onnxruntime.dll");
    ASSERT_TRUE(QFile::remove(userDirectory + "/onnxruntime.dll"));
    EXPECT_EQ(pfservices::ProviderManager::locateRuntime({userRoot.path(), appRoot.path()}, "cuda"),
        appDirectory + "/onnxruntime.dll");
    EXPECT_TRUE(pfservices::ProviderManager::locateRuntime({userRoot.path()}, "../cuda").isEmpty());
}

TEST(FfmpegCapabilities, MissingHardwareFallsBackToInstalledSoftwareOnly)
{
    const auto caps = pfservices::FfmpegCapabilities::parse(
        "Encoders:\n V..... = Video\n ------\n V....D libx264 H.264 encoder\n"
        " A....D aac AAC encoder\n S..... subrip subtitles\n V..... mpeg4 MPEG4\n");
    EXPECT_EQ(caps.h264Candidates(), (std::vector<std::string>{"libx264"}));
    EXPECT_EQ(caps.audioEncoders, (std::vector<std::string>{"aac"}));
}

TEST(FfmpegCapabilities, OrdersAvailableHardwareThenSoftwareWithoutGuessing)
{
    const auto caps = pfservices::FfmpegCapabilities::parse(
        " V....D libx264 software\n V....D h264_qsv Intel\n V....D h264_nvenc NVIDIA\n");
    EXPECT_EQ(caps.h264Candidates(),
        (std::vector<std::string>{"h264_nvenc", "h264_qsv", "libx264"}));
    EXPECT_TRUE(pfservices::FfmpegCapabilities::parse(" V..... mpeg4 codec\n").h264Candidates().empty());
}

TEST(ExportQueue, ContinuesAfterItemFailuresAndReportsEveryItem)
{
    std::vector<pfservices::CutRequest> jobs(200);
    std::size_t reported = 0;
    const auto result = pfservices::runExportQueue(jobs, {},
        [&](std::size_t done, std::size_t total) {
            EXPECT_EQ(total, 200u);
            EXPECT_EQ(done, ++reported);
        }, "nonexistent-ffmpeg");
    EXPECT_EQ(result.completed.size(), 200u);
    EXPECT_EQ(reported, 200u);
    EXPECT_FALSE(result.cancelled);
    for (const auto& item : result.completed) EXPECT_FALSE(item.success);
}

TEST(ExportQueue, CancellationLeavesRemainingJobsUnstarted)
{
    std::stop_source stop;
    const auto result = pfservices::runExportQueue(
        std::vector<pfservices::CutRequest>(100), stop.get_token(),
        [&](std::size_t, std::size_t) { stop.request_stop(); }, "nonexistent-ffmpeg");
    EXPECT_EQ(result.completed.size(), 1u);
    EXPECT_TRUE(result.cancelled);
}

TEST(CutService, CancellationStopsAnActiveEncoderAndCleansTemporaryOutput)
{
    const auto ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    if (ffmpeg.isEmpty()) GTEST_SKIP() << "FFmpeg unavailable";
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto input = directory.filePath("long.mp4");
    QProcess fixture;
    fixture.start(ffmpeg, {"-hide_banner", "-loglevel", "error", "-y",
        "-f", "lavfi", "-i", "color=size=640x360:rate=30:duration=60",
        "-c:v", "mpeg4", input});
    ASSERT_TRUE(fixture.waitForFinished(30000));
    ASSERT_EQ(fixture.exitCode(), 0);
    pfservices::CutRequest request;
    request.inputPath = input.toStdWString();
    const pfservices::CutService cutter(ffmpeg.toStdString());
    request.outputPath = directory.filePath("warm.mp4").toStdWString();
    request.endSeconds = 0.1;
    ASSERT_TRUE(cutter.cut(request).success);
    request.outputPath = directory.filePath("cancelled.mp4").toStdWString();
    request.endSeconds = 60;
    std::stop_source stop;
    request.stopToken = stop.get_token();
    std::atomic_bool encoderStarted{false};
    std::jthread cancel([&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            if (QFileInfo::exists(directory.filePath("cancelled.part.mp4"))) {
                encoderStarted.store(true);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        stop.request_stop();
    });
    const auto result = cutter.cut(request);
    cancel.join();
    EXPECT_TRUE(encoderStarted.load());
    EXPECT_FALSE(result.success);
    EXPECT_TRUE(result.cancelled);
    EXPECT_FALSE(std::filesystem::exists(request.outputPath));
    EXPECT_FALSE(QFileInfo::exists(directory.filePath("cancelled.part.mp4")));
}

TEST(CutService, ExportsVideoWithSubtitlesToMp4)
{
    const auto ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    if (ffmpeg.isEmpty()) GTEST_SKIP() << "FFmpeg integration dependency unavailable";
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    QFile subtitles(directory.filePath("captions.srt"));
    ASSERT_TRUE(subtitles.open(QIODevice::WriteOnly));
    subtitles.write("1\n00:00:00,000 --> 00:00:01,000\nCaption\n");
    subtitles.close();
    const auto input = directory.filePath("input.mkv");
    QProcess fixture;
    fixture.start(ffmpeg, {"-hide_banner", "-loglevel", "error", "-y",
        "-f", "lavfi", "-i", "color=size=64x64:rate=10:duration=1",
        "-i", subtitles.fileName(), "-map", "0:v", "-map", "1:s",
        "-c:v", "mpeg4", "-c:s", "srt", input});
    ASSERT_TRUE(fixture.waitForFinished(30000));
    ASSERT_EQ(fixture.exitCode(), 0) << fixture.readAllStandardError().toStdString();
    pfservices::CutService service(ffmpeg.toStdString());
    for (const auto mode : {pfservices::CutMode::Exact, pfservices::CutMode::Fast}) {
        pfservices::CutRequest request;
        request.inputPath = input.toStdWString();
        request.outputPath = directory.filePath(mode == pfservices::CutMode::Exact
            ? "exact.mp4" : "fast.mp4").toStdWString();
        request.endSeconds = 0.5;
        request.mode = mode;
        const auto result = service.cut(request);
        EXPECT_TRUE(result.success) << result.error;
        EXPECT_TRUE(std::filesystem::exists(request.outputPath));
        if (result.success) {
            QProcess decode;
            decode.start(ffmpeg, {"-hide_banner", "-loglevel", "error",
                "-i", QString::fromStdWString(request.outputPath.wstring()),
                "-map", "0:v:0", "-f", "null", "-"});
            ASSERT_TRUE(decode.waitForFinished(30000));
            EXPECT_EQ(decode.exitStatus(), QProcess::NormalExit);
            EXPECT_EQ(decode.exitCode(), 0) << decode.readAllStandardError().toStdString();
        }
        const auto temporaryPath = request.outputPath.parent_path()
            / (request.outputPath.stem().string() + ".part.mp4");
        EXPECT_FALSE(std::filesystem::exists(temporaryPath));
    }
}

TEST(CutService, RejectsInvalidRangeBeforeStartingFfmpeg)
{
    pfservices::CutService service("definitely-not-a-real-ffmpeg");
    pfservices::CutRequest request;
    request.inputPath = "missing.mp4";
    request.outputPath = "out.mp4";
    request.startSeconds = 4.0;
    request.endSeconds = 2.0;

    const auto result = service.cut(request);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error, "invalid cut time range");
}

TEST(CutService, RejectsMissingInputWithoutCreatingOutput)
{
    const auto output = std::filesystem::temp_directory_path() / "parallel-finder-cut-test.mp4";
    std::error_code ignored;
    std::filesystem::remove(output, ignored);

    pfservices::CutService service("definitely-not-a-real-ffmpeg");
    pfservices::CutRequest request;
    request.inputPath = "missing.mp4";
    request.outputPath = output;
    request.startSeconds = 0.0;
    request.endSeconds = 1.0;

    const auto result = service.cut(request);
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(std::filesystem::exists(output));
}

TEST(CutService, RejectsInvalidResolutionBeforeStartingFfmpeg)
{
    pfservices::CutService service("definitely-not-a-real-ffmpeg");
    pfservices::CutRequest request;
    request.inputPath = "missing.mp4";
    request.outputPath = "out.mp4";
    request.startSeconds = 0.0;
    request.endSeconds = 1.0;
    request.maxWidth = -1;

    const auto result = service.cut(request);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error, "maximum output dimensions must not be negative");
}

TEST(CutService, FastModeCannotResize)
{
    pfservices::CutService service("definitely-not-a-real-ffmpeg");
    pfservices::CutRequest request;
    request.inputPath = "missing.mp4";
    request.outputPath = "out.mp4";
    request.startSeconds = 0.0;
    request.endSeconds = 1.0;
    request.mode = pfservices::CutMode::Fast;
    request.maxWidth = 1920;

    const auto result = service.cut(request);
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error, "resolution cap requires exact cut mode");
}
