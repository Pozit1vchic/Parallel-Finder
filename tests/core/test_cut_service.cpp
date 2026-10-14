#include "../../services/src/MontageGeometry.hpp"
#include <gtest/gtest.h>

#include <pfservices/CutService.hpp>
#include <pfservices/ExportQueue.hpp>
#include <pfservices/MontageExport.hpp>
#include <pfservices/RuntimeScratch.hpp>
#include <pfservices/FfmpegCapabilities.hpp>
#include <pfservices/ProviderManager.hpp>
#include <pfservices/PreviewMemo.hpp>
#include <pfcore/VideoDecoder.hpp>
#include <pfcore/SceneDetector.hpp>
#include <pfcore/ParallelClip.hpp>
#include <pfcore/DominantPerson.hpp>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include <filesystem>
#include <limits>

TEST(ParallelClip, PreservesSupportedMatchWithoutPadding)
{
    const auto clip = pfcore::parallelClipRange(10, 12, 0, 60);
    EXPECT_DOUBLE_EQ(clip.start, 10);
    EXPECT_DOUBLE_EQ(clip.end, 12);
}

TEST(ParallelClip, DoesNotBorrowContextAtShotEdges)
{
    const auto nearStart = pfcore::parallelClipRange(10, 10.5, 10, 30);
    EXPECT_DOUBLE_EQ(nearStart.start, 10);
    EXPECT_DOUBLE_EQ(nearStart.end, 10.5);
    const auto nearEnd = pfcore::parallelClipRange(28.5, 29.5, 10, 30);
    EXPECT_DOUBLE_EQ(nearEnd.start, 28.5);
    EXPECT_DOUBLE_EQ(nearEnd.end, 29.5);
}

TEST(ParallelClip, ShortShotNeverIncludesNextScene)
{
    const auto clip = pfcore::parallelClipRange(11, 11.5, 10, 13);
    EXPECT_DOUBLE_EQ(clip.start, 11);
    EXPECT_DOUBLE_EQ(clip.end, 11.5);
}

TEST(ParallelClip, LongGestureIsNotTruncated)
{
    const auto clip = pfcore::parallelClipRange(10, 20, 0, 60);
    EXPECT_DOUBLE_EQ(clip.start, 10);
    EXPECT_DOUBLE_EQ(clip.end, 20);
}

TEST(ParallelClip, StaticMatchAndUnknownSceneStillHaveBoundedContext)
{
    const auto clip = pfcore::parallelClipRange(10, 10, -1, -1);
    EXPECT_DOUBLE_EQ(clip.start, 10);
    EXPECT_NEAR(clip.end, 10 + 1.0 / 24.0, 1e-12);
}

TEST(ParallelClip, HighFrameRateMatchDoesNotGainExtraFrames)
{
    const auto shortMatch=pfcore::parallelClipRange(10,10.02,0,60,50);
    EXPECT_DOUBLE_EQ(shortMatch.end,10.02);
    const auto single=pfcore::parallelClipRange(10,10,0,60,60);
    EXPECT_NEAR(single.end,10+1.0/60.0,1e-12);
    const auto fallback=pfcore::parallelClipRange(10,10,-1,-1,std::numeric_limits<double>::quiet_NaN());
    EXPECT_NEAR(fallback.end,10+1.0/24.0,1e-12);
}
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

namespace {
void verifyOrderedBoundaryRefinement(const QString& path, bool nvidia, double start, double end)
{
    pfcore::VideoDecodeOptions options; options.preferNvidia = nvidia;
    pfcore::VideoDecoder batch, serial; batch.open(path.toStdString(), options); serial.open(path.toStdString(), options);
    if (nvidia) { ASSERT_EQ(batch.diagnostics().backend, "nvdec"); }
    pfcore::SceneDetector detector;
    std::vector<double> times, poses;
    std::vector<pfcore::SceneBoundary> boundaries;
    for (double t = start; t <= end; t += .25) times.push_back(t);
    for (double t = start; t <= end; t += 1./24) poses.push_back(t);
    for (std::size_t i = 1; i < times.size(); ++i)
        if (i % 4 == 0 || i % 4 == 1 || i % 19 == 0) boundaries.push_back({times[i], .7});
    std::vector<pfcore::SceneBoundary> expected;
    for (const auto& boundary : boundaries) {
        const auto sample = std::lower_bound(times.begin(), times.end(), boundary.timestampSeconds);
        const double before = *std::prev(sample);
        auto resolved = boundary;
        const auto pose = std::lower_bound(poses.begin(), poses.end(), before);
        if (pose != poses.end() && *pose < boundary.timestampSeconds) {
            const auto native = detector.detectInterval(serial, before, boundary.timestampSeconds + 1e-4);
            if (!native.empty() && native.back().timestampSeconds <= boundary.timestampSeconds + 1e-6
                && (expected.empty() || native.back().timestampSeconds > expected.back().timestampSeconds))
                resolved = native.back();
        }
        expected.push_back(resolved);
    }
    std::size_t previous = 0;
    const auto caller = std::this_thread::get_id();
    const auto actual = detector.refineSupportedBoundaries(batch, boundaries, times, poses, {},
        [&](std::size_t done, std::size_t total) {
            EXPECT_EQ(std::this_thread::get_id(), caller);
            EXPECT_EQ(total, boundaries.size()); EXPECT_GE(done, previous); EXPECT_LE(done, total);
            previous = done;
        });
    EXPECT_EQ(previous, boundaries.size());
    ASSERT_EQ(actual.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
        EXPECT_DOUBLE_EQ(actual[i].timestampSeconds, expected[i].timestampSeconds) << i;
        EXPECT_DOUBLE_EQ(actual[i].score, expected[i].score) << i;
    }
    EXPECT_LT(batch.diagnostics().decodedFrames, serial.diagnostics().decodedFrames);
    for(const auto workers:{std::size_t(2),std::size_t(4)}) {
        if(nvidia && workers>2)continue;
        previous=0;
        const auto parallel=detector.refineSupportedBoundariesParallel(path.toStdString(),options,
            boundaries,times,poses,workers,{},[&](auto done,auto total){
                EXPECT_EQ(std::this_thread::get_id(),caller);
                EXPECT_EQ(total,boundaries.size());EXPECT_GE(done,previous);EXPECT_LE(done,total);previous=done;
            });
        EXPECT_EQ(previous,boundaries.size());ASSERT_EQ(parallel.size(),expected.size());
        for(std::size_t i=0;i<expected.size();++i){
            EXPECT_DOUBLE_EQ(parallel[i].timestampSeconds,expected[i].timestampSeconds)<<"workers="<<workers<<" boundary="<<i;
            EXPECT_DOUBLE_EQ(parallel[i].score,expected[i].score)<<"workers="<<workers<<" boundary="<<i;
        }
    }
    EXPECT_TRUE(detector.refineSupportedBoundariesParallel(path.toStdString(),options,
        boundaries,times,poses,2,[]{return true;}).empty());
    std::atomic_bool cancel{false};
    EXPECT_TRUE(detector.refineSupportedBoundariesParallel(path.toStdString(),options,
        boundaries,times,poses,2,[&]{return cancel.load();},[&](auto done,auto){if(done>0)cancel.store(true);}).empty());
}
}

TEST(SceneBoundaryExport, OrderedRefinementMatchesIndependentIntervalsOnLongGopAndVfr)
{
    const auto ffmpeg = QStandardPaths::findExecutable("ffmpeg"); ASSERT_FALSE(ffmpeg.isEmpty());
    QTemporaryDir dir;
    for (const bool vfr : {false, true}) {
        const auto path = dir.filePath(vfr ? "vfr.mp4" : "cfr.mp4");
        QProcess generate;
        const QString filter = "drawbox=c=red:t=fill:enable='lt(mod(t,2),1)',drawbox=c=blue:t=fill:enable='gte(mod(t,2),1)'"
            + QString(vfr ? ",select='not(eq(mod(n,5),0))'" : "");
        generate.start(ffmpeg, {"-v","error","-f","lavfi","-i","color=black:s=128x72:r=24:d=35",
            "-vf",filter,"-fps_mode","vfr","-c:v","libx264","-g","240","-bf","3","-threads","2",path});
        ASSERT_TRUE(generate.waitForFinished(30000)); ASSERT_EQ(generate.exitCode(),0) << generate.readAllStandardError().toStdString();
        verifyOrderedBoundaryRefinement(path, false, .125, 34.125);
    }
}

TEST(SceneBoundaryExport, RealTenBitOrderedNvidiaRefinementMatchesIndependentIntervals)
{
    const auto path = qEnvironmentVariable("PF_TEST_10BIT_SOURCE");
    if (path.isEmpty()) GTEST_SKIP() << "Opt-in real 10-bit ordered NVIDIA boundary refinement";
    for (const double start : {0.125, 560.125, 1220.125})
        verifyOrderedBoundaryRefinement(path, true, start, start + 12);
}

TEST(RuntimeScratch, RemovesOwnExpiredFilesButProtectsActiveSessionsAndUnknownFiles)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    const auto old = std::filesystem::file_time_type::clock::now() - std::chrono::hours(48);
    const auto write = [&](const QString& name) {
        QFile file(root.filePath(name));
        if (!file.open(QIODevice::WriteOnly)) return false;
        file.write("keep"); file.close();
        std::filesystem::last_write_time(std::filesystem::path(file.fileName().toStdWString()), old);
        return true;
    };
    ASSERT_TRUE(write("123_match_frame_1.png"));
    ASSERT_TRUE(write("123_1_start.png"));
    ASSERT_TRUE(write("user.png"));
    ASSERT_TRUE(QDir().mkpath(root.filePath("pf-session-ABC123")));
    QString firstPath, secondPath;
    {
        pfservices::RuntimeScratch first(root.path(), 0);
        firstPath = first.path(); ASSERT_FALSE(firstPath.isEmpty());
        pfservices::RuntimeScratch second(root.path(), 0);
        secondPath = second.path(); ASSERT_FALSE(secondPath.isEmpty());
        EXPECT_NE(firstPath, secondPath);
        EXPECT_TRUE(QFileInfo::exists(firstPath));
        EXPECT_FALSE(QFileInfo::exists(root.filePath("pf-session-ABC123")));
        EXPECT_FALSE(QFileInfo::exists(root.filePath("123_match_frame_1.png")));
        EXPECT_FALSE(QFileInfo::exists(root.filePath("123_1_start.png")));
        EXPECT_TRUE(QFileInfo::exists(root.filePath("user.png")));
    }
    EXPECT_FALSE(QFileInfo::exists(firstPath)); EXPECT_FALSE(QFileInfo::exists(secondPath));
    EXPECT_TRUE(QFileInfo::exists(root.filePath("user.png")));
}

TEST(SceneBoundaryExport, RefinesLateSparseBoundaryAndExcludesTheNextShot)
{
    const auto ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    ASSERT_FALSE(ffmpeg.isEmpty());
    QTemporaryDir directory; ASSERT_TRUE(directory.isValid());
    const auto source = directory.filePath("two-shots.mp4");
    QProcess generate;
    generate.start(ffmpeg, {"-v", "error", "-f", "lavfi", "-i", "color=c=red:s=128x72:r=24:d=2",
        "-f", "lavfi", "-i", "color=c=blue:s=128x72:r=24:d=2", "-filter_complex",
        "[0:v]trim=end_frame=23,setpts=PTS-STARTPTS[a];[1:v]trim=end_frame=24,setpts=PTS-STARTPTS[b];[a][b]concat=n=2:v=1:a=0[v]",
        "-map", "[v]", "-c:v", "libx264", "-threads", "2", "-pix_fmt", "yuv420p", source});
    ASSERT_TRUE(generate.waitForFinished(30000));
    ASSERT_EQ(generate.exitCode(), 0) << generate.readAllStandardError().toStdString();
    pfcore::VideoDecoder decoder; decoder.open(source.toStdString());
    pfcore::SceneDetector detector;
    const auto boundary = detector.refineHardCut(decoder, 1.0);
    ASSERT_TRUE(boundary);
    EXPECT_NEAR(*boundary, 23.0 / 24.0, 1e-6);
    EXPECT_FALSE(detector.refineHardCut(decoder, .5)); // no fabricated cut
    const auto internalCuts = detector.detectInterval(decoder, .25, 1.75);
    ASSERT_EQ(internalCuts.size(), 1U);
    EXPECT_NEAR(internalCuts[0].timestampSeconds, 23.0 / 24.0, 1e-6);
    EXPECT_TRUE(detector.detectInterval(decoder, .25, .75).empty());
    EXPECT_TRUE(detector.detectInterval(decoder, 23.0 / 24.0, 1.75).empty());
    EXPECT_TRUE(detector.detectInterval(decoder, .25, 23.0 / 24.0).empty());
    EXPECT_TRUE(detector.detectInterval(decoder, 1, .5).empty());
    EXPECT_TRUE(detector.detectInterval(decoder, std::numeric_limits<double>::quiet_NaN(), 1).empty());
    const pfcore::SceneBoundary sampledCut{1,1};
    const std::vector<double> sampleTimes{0,.25,.5,.75,1,1.25,1.5};
    const std::vector<double> observedPoses{.8,.9,23.0/24,1,1.1,1.2};
    const auto corrected=detector.refineSupportedBoundaries(decoder,std::span(&sampledCut,1),sampleTimes,observedPoses);
    ASSERT_EQ(corrected.size(),1U);
    EXPECT_NEAR(corrected[0].timestampSeconds,23.0/24,1e-6);
    const auto confirmed=detector.refineSupportedBoundaries(decoder,std::span(&sampledCut,1),
        sampleTimes,observedPoses,{}, {}, true);
    ASSERT_EQ(confirmed.size(),1U);
    EXPECT_NEAR(confirmed[0].timestampSeconds,23.0/24,1e-6);
    const pfcore::SceneBoundary unsupported{.5,1};
    const std::vector<double> proposalPoses{.3,.4,.8,.9,1,1.1};
    std::size_t confirmedProgress=0,confirmedTotal=0;
    EXPECT_TRUE(detector.refineSupportedBoundaries(decoder,std::span(&unsupported,1),
        sampleTimes,proposalPoses,{},[&](auto done,auto total) {
            confirmedProgress=done;confirmedTotal=total;
        },true).empty());
    EXPECT_EQ(confirmedProgress,1U); EXPECT_EQ(confirmedTotal,1U);
    const std::vector<double> noAffectedPoses{.25,.5,1.1,1.2};
    const auto unchanged=detector.refineSupportedBoundaries(decoder,std::span(&sampledCut,1),sampleTimes,noAffectedPoses);
    ASSERT_EQ(unchanged.size(),1U);EXPECT_DOUBLE_EQ(unchanged[0].timestampSeconds,1);
    EXPECT_TRUE(detector.refineSupportedBoundaries(decoder,std::span(&sampledCut,1),
        sampleTimes,noAffectedPoses,{}, {},true).empty());
    EXPECT_TRUE(detector.refineSupportedBoundaries(decoder,std::span(&sampledCut,1),sampleTimes,observedPoses,[]{return true;}).empty());
    bool progressCancelled = false;
    EXPECT_TRUE(detector.refineSupportedBoundaries(decoder,std::span(&sampledCut,1),sampleTimes,observedPoses,
        [&] { return progressCancelled; }, [&](auto done,auto) { if(done==1)progressCancelled=true; }).empty());
    EXPECT_TRUE(detector.detectInterval(decoder,.25,1.75,{},[]{return true;}).empty());
    // Late boundaries contaminated the original track's shot prototype with
    // the first face from the next plan. Native correction must precede the
    // track split; every new track must have unmixed identity observations.
    pfcore::PersonTracker tracker(.3,1,false);
    for(const double time:observedPoses) {
        pfcore::PersonDetection person;person.timestampSeconds=time;person.frameDurationSeconds=1./24;
        person.box={0,0,100,100};person.confidence=person.keypointConfidence=.9;
        person.faceEmbedding=time<corrected[0].timestampSeconds ? std::vector<float>{1,0}:std::vector<float>{0,1};
        tracker.update(time,1./24,{person});
    }
    tracker.retrackScenes({corrected[0].timestampSeconds});
    ASSERT_EQ(tracker.tracks().size(),2U);
    for(const auto& track:tracker.tracks()) {
        ASSERT_FALSE(track.observations.empty());
        for(const auto& observation:track.observations)
            EXPECT_EQ(observation.faceEmbedding,track.observations.front().faceEmbedding);
    }
    std::stop_source cancelled; cancelled.request_stop();
    EXPECT_FALSE(detector.refineHardCut(decoder, 1, cancelled.get_token()));
    EXPECT_TRUE(detector.detectInterval(decoder, .25, 1.75, cancelled.get_token()).empty());
    pfservices::CutRequest request;
    request.inputPath = std::filesystem::path(source.toStdWString());
    request.outputPath = std::filesystem::path(directory.filePath("red-only.mp4").toStdWString());
    request.endSeconds = *boundary;
    ASSERT_TRUE(pfservices::CutService(ffmpeg.toStdString()).cut(request).success);
    pfcore::VideoDecoder exported; exported.open(request.outputPath.string());
    pfcore::DecodedFrame frame;
    int count = 0;
    while (exported.readNext(frame)) {
        ++count;
        ASSERT_FALSE(frame.rgba.empty());
        EXPECT_GT(frame.rgba[0], frame.rgba[2]); // no blue frame from following shot
    }
    EXPECT_EQ(count, 23);
}

TEST(SceneBoundaryExport, ChoosesNearestCutInsteadOfStrongerEarlierCut)
{
    const auto ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    ASSERT_FALSE(ffmpeg.isEmpty());
    QTemporaryDir directory; ASSERT_TRUE(directory.isValid());
    const auto source = directory.filePath("three-shots.mp4");
    QProcess generate;
    generate.start(ffmpeg, {"-v", "error", "-f", "lavfi", "-i", "color=c=black:s=128x72:r=24:d=2",
        "-f", "lavfi", "-i", "color=c=white:s=128x72:r=24:d=2",
        "-f", "lavfi", "-i", "color=c=gray:s=128x72:r=24:d=2", "-filter_complex",
        "[0:v]trim=end_frame=18,setpts=PTS-STARTPTS[a];[1:v]trim=end_frame=5,setpts=PTS-STARTPTS[b];[2:v]trim=end_frame=24,setpts=PTS-STARTPTS[c];[a][b][c]concat=n=3:v=1:a=0[v]",
        "-map", "[v]", "-c:v", "libx264", "-threads", "2", "-pix_fmt", "yuv420p", source});
    ASSERT_TRUE(generate.waitForFinished(30000));
    ASSERT_EQ(generate.exitCode(),0) << generate.readAllStandardError().toStdString();
    pfcore::VideoDecoder decoder; decoder.open(source.toStdString());
    const auto boundary = pfcore::SceneDetector().refineHardCut(decoder,1.0);
    ASSERT_TRUE(boundary);
    EXPECT_NEAR(*boundary,23.0/24.0,1e-6);
}

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
    const bool sourceHasAudio = std::any_of(streams.begin(), streams.end(), [](const auto& stream) {
        return stream.toObject().value("codec_type").toString() == "audio";
    });
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
        EXPECT_EQ(audio, sourceHasAudio);
        EXPECT_GE(metadata.value("format").toObject().value("duration").toString().toDouble(), 26.9);
    }
}

TEST(MontageExport, SortsDeduplicatesAndNormalizesMixedSilentAndAudioVideos)
{
    const auto ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    if (ffmpeg.isEmpty()) GTEST_SKIP() << "FFmpeg unavailable";
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    const auto red = temporary.filePath("red.mp4");
    const auto blue = temporary.filePath("blue.mp4");
    QProcess generator;
    generator.start(ffmpeg, {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi",
        "-i", "color=c=red:s=160x90:r=10:d=3", "-c:v", "libx264", red});
    ASSERT_TRUE(generator.waitForFinished(30000));
    ASSERT_EQ(generator.exitCode(), 0) << generator.readAllStandardError().toStdString();
    generator.start(ffmpeg, {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi",
        "-i", "color=c=blue:s=320x180:r=15:d=3", "-f", "lavfi", "-i", "sine=frequency=440:duration=3",
        "-c:v", "libx264", "-c:a", "aac", "-shortest", blue});
    ASSERT_TRUE(generator.waitForFinished(30000));
    ASSERT_EQ(generator.exitCode(), 0) << generator.readAllStandardError().toStdString();
    pfservices::CutRequest a, b;
    a.inputPath = blue.toStdWString(); a.startSeconds = 1; a.endSeconds = 2;
    b.inputPath = red.toStdWString(); b.startSeconds = 0; b.endSeconds = 1;
    const auto output = temporary.filePath("montage.mp4");
    std::size_t done = 0, total = 0;
    const auto exported = pfservices::exportChronologicalMontage({a, b, a}, output.toStdWString(), {},
        [&](std::size_t completed, std::size_t expected, int) { done = completed; total = expected; }, ffmpeg.toStdString());
    ASSERT_TRUE(exported.success) << exported.error;
    EXPECT_EQ(done, 3U); EXPECT_EQ(total, 3U); // two unique clips and final mux
    pfcore::VideoDecoder decoder;
    decoder.open(output.toStdString());
    EXPECT_EQ(decoder.info().width, 160); EXPECT_EQ(decoder.info().height, 90);
    EXPECT_NEAR(decoder.info().frameRate, 10, .01);
    EXPECT_TRUE(decoder.info().hasAudio);
    EXPECT_NEAR(decoder.info().durationSeconds, 2, .3);
    pfcore::DecodedFrame frame;
    ASSERT_TRUE(decoder.readNext(frame));
    ASSERT_GE(frame.rgba.size(), 4U);
    EXPECT_GT(frame.rgba[0], frame.rgba[2] + 100); // chronological red first
    decoder.seek(1.5);
    ASSERT_TRUE(decoder.readNext(frame));
    EXPECT_GT(frame.rgba[2], frame.rgba[0] + 100); // blue second
    decoder.close();
    // Movement groups override chronology and deduplicate repeated clips
    // even when the duplicates are separated by another group.
    const auto grouped = pfservices::exportChronologicalMontage({a, b, a},
        temporary.filePath("grouped.mp4").toStdWString(), {}, {}, ffmpeg.toStdString(), true);
    ASSERT_TRUE(grouped.success) << grouped.error;
    decoder.open(QString::fromStdWString(grouped.outputPath.wstring()).toStdString());
    EXPECT_NEAR(decoder.info().durationSeconds, 2, .3);
    ASSERT_TRUE(decoder.readNext(frame));
    EXPECT_GT(frame.rgba[2], frame.rgba[0] + 100); // requested blue first
    decoder.seek(1.5);
    ASSERT_TRUE(decoder.readNext(frame));
    EXPECT_GT(frame.rgba[0], frame.rgba[2] + 100); // red second
    EXPECT_TRUE(QDir(temporary.path()).entryList({".parallelfinder-montage-*"}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());
}

TEST(MontageExport, CancellationPreservesDestinationAndRemovesStaging)
{
    const auto ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    if (ffmpeg.isEmpty()) GTEST_SKIP() << "FFmpeg unavailable";
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    const auto source = temporary.filePath("source.mp4");
    QProcess generator;
    generator.start(ffmpeg, {"-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi",
        "-i", "color=c=red:s=160x90:r=10:d=3", "-c:v", "libx264", source});
    ASSERT_TRUE(generator.waitForFinished(30000));
    ASSERT_EQ(generator.exitCode(), 0);
    pfservices::CutRequest a, b;
    a.inputPath = b.inputPath = source.toStdWString();
    a.startSeconds = 0; a.endSeconds = 1; b.startSeconds = 1; b.endSeconds = 2;
    const auto output = temporary.filePath("montage.mp4");
    { QFile previous(output); ASSERT_TRUE(previous.open(QIODevice::WriteOnly)); previous.write("previous"); }
    std::stop_source stop;
    const auto exported = pfservices::exportChronologicalMontage({a, b}, output.toStdWString(), stop.get_token(),
        [&](std::size_t completed, std::size_t, int) { if (completed > 0) stop.request_stop(); }, ffmpeg.toStdString());
    EXPECT_FALSE(exported.success); EXPECT_TRUE(exported.cancelled);
    QFile previous(output); ASSERT_TRUE(previous.open(QIODevice::ReadOnly));
    EXPECT_EQ(previous.readAll(), "previous");
    EXPECT_TRUE(QDir(temporary.path()).entryList({".parallelfinder-montage-*"}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty());
}

TEST(MontageExport, LosslessClipsAndCombinedVideoKeepOriginalVideoAndAudioPackets)
{
    const auto ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    const auto ffprobe = QStandardPaths::findExecutable("ffprobe");
    ASSERT_FALSE(ffmpeg.isEmpty()); ASSERT_FALSE(ffprobe.isEmpty());
    QTemporaryDir directory; ASSERT_TRUE(directory.isValid());
    const auto source = directory.filePath("original.mp4");
    QProcess generator;
    generator.start(ffmpeg, {"-v", "error", "-f", "lavfi", "-i", "testsrc2=s=640x360:r=24:d=4",
        "-f", "lavfi", "-i", "sine=frequency=440:duration=4", "-f", "lavfi", "-i", "sine=frequency=660:duration=4",
        "-map", "0:v", "-map", "1:a", "-map", "2:a", "-c:v", "libx264", "-g", "24", "-threads", "2",
        "-c:a", "aac", "-shortest", source});
    ASSERT_TRUE(generator.waitForFinished(30000)); ASSERT_EQ(generator.exitCode(), 0);
    const auto packets = [&](const QString& path) {
        QProcess probe;
        probe.start(ffprobe, {"-v", "error", "-show_packets", "-show_data_hash", "sha256",
            "-show_entries", "packet=stream_index,data_hash", "-of", "json", path});
        if (!probe.waitForFinished(30000) || probe.exitCode() != 0) return QJsonArray{};
        return QJsonDocument::fromJson(probe.readAllStandardOutput()).object().value("packets").toArray();
    };
    const auto original = packets(source); ASSERT_FALSE(original.isEmpty());
    const auto checkOriginalPackets = [&](const QString& path) {
        const auto copied = packets(path); ASSERT_FALSE(copied.isEmpty());
        std::set<int> streams;
        for (const auto& packet : copied) {
            const auto value = packet.toObject();
            streams.insert(value.value("stream_index").toInt());
            EXPECT_TRUE(std::any_of(original.begin(), original.end(), [&](const auto& candidate) {
                const auto encoded = candidate.toObject();
                return encoded.value("stream_index") == value.value("stream_index")
                    && encoded.value("data_hash") == value.value("data_hash");
            })) << "Export modified an encoded video/audio packet";
        }
        EXPECT_EQ(streams.size(), 3u); // video and both original audio tracks
        pfcore::VideoDecoder decoder; decoder.open(path.toStdString());
        pfcore::DecodedFrame frame; EXPECT_TRUE(decoder.readNext(frame));
        EXPECT_EQ(decoder.info().width, 640); EXPECT_EQ(decoder.info().height, 360);
    };
    pfservices::CutRequest a, b;
    a.inputPath = b.inputPath = source.toStdWString();
    a.startSeconds = .5; a.endSeconds = 1.2; a.mode = pfservices::CutMode::Fast;
    b.startSeconds = 2.5; b.endSeconds = 3.2; b.mode = pfservices::CutMode::Fast;
    const auto clip = directory.filePath("clip.mp4"); a.outputPath = clip.toStdWString();
    const auto cut = pfservices::CutService(ffmpeg.toStdString()).cut(a);
    ASSERT_TRUE(cut.success) << cut.error; EXPECT_EQ(cut.encoder, "copy"); checkOriginalPackets(clip);
    const auto combined = directory.filePath("combined.mp4");
    const auto montage = pfservices::exportChronologicalMontage({b, a, b}, combined.toStdWString(), {}, {}, ffmpeg.toStdString());
    ASSERT_TRUE(montage.success) << montage.error; checkOriginalPackets(combined);
}

TEST(MontageExport, LosslessRejectsIncompatibleSourcesWithoutReplacingDestination)
{
    const auto ffmpeg = QStandardPaths::findExecutable("ffmpeg"); ASSERT_FALSE(ffmpeg.isEmpty());
    QTemporaryDir directory; ASSERT_TRUE(directory.isValid());
    const auto other = directory.filePath("other.mp4");
    QProcess generator;
    generator.start(ffmpeg, {"-v", "error", "-f", "lavfi", "-i", "color=s=160x90:r=24:d=1", "-c:v", "libx264", other});
    ASSERT_TRUE(generator.waitForFinished(30000)); ASSERT_EQ(generator.exitCode(), 0);
    pfservices::CutRequest a, b;
    a.inputPath = std::filesystem::path(PF_TEST_FIXTURE_DIR) / "tiny.mp4";
    b.inputPath = other.toStdWString(); a.endSeconds = b.endSeconds = .2;
    a.mode = b.mode = pfservices::CutMode::Fast;
    const auto output = directory.filePath("keep.mp4");
    { QFile file(output); ASSERT_TRUE(file.open(QIODevice::WriteOnly)); file.write("previous"); }
    const auto result = pfservices::exportChronologicalMontage({a, b}, output.toStdWString(), {}, {}, ffmpeg.toStdString());
    EXPECT_FALSE(result.success); EXPECT_NE(result.error.find("compatible"), std::string::npos);
    QFile file(output); ASSERT_TRUE(file.open(QIODevice::ReadOnly)); EXPECT_EQ(file.readAll(), "previous");
    a.mode=b.mode=pfservices::CutMode::ExactLossless;
    const auto losslessOutput=directory.filePath("keep.mov");
    {QFile sentinel(losslessOutput);ASSERT_TRUE(sentinel.open(QIODevice::WriteOnly));sentinel.write("previous");}
    const auto lossless=pfservices::exportChronologicalMontage({a,b},losslessOutput.toStdWString(),{},{},ffmpeg.toStdString());
    EXPECT_FALSE(lossless.success);EXPECT_NE(lossless.error.find("compatible"),std::string::npos);
    {QFile sentinel(losslessOutput);ASSERT_TRUE(sentinel.open(QIODevice::ReadOnly));EXPECT_EQ(sentinel.readAll(),"previous");}
    a.maxWidth=80;
    const auto resized=pfservices::exportChronologicalMontage({a},losslessOutput.toStdWString(),{},{},ffmpeg.toStdString());
    EXPECT_FALSE(resized.success);EXPECT_NE(resized.error.find("normalize"),std::string::npos);
    {QFile sentinel(losslessOutput);ASSERT_TRUE(sentinel.open(QIODevice::ReadOnly));EXPECT_EQ(sentinel.readAll(),"previous");}
}

TEST(MontageExport, RejectsEmptyRequestsAndSourceReplacement)
{
    QTemporaryDir temporary;
    const auto path = temporary.filePath("keep.mp4");
    { QFile sentinel(path); ASSERT_TRUE(sentinel.open(QIODevice::WriteOnly)); sentinel.write("keep"); }
    EXPECT_FALSE(pfservices::exportChronologicalMontage({}, path.toStdWString()).success);
    pfservices::CutRequest clip; clip.inputPath = path.toStdWString(); clip.endSeconds = 1;
    EXPECT_FALSE(pfservices::exportChronologicalMontage({clip}, path.toStdWString()).success);
    QFile sentinel(path); ASSERT_TRUE(sentinel.open(QIODevice::ReadOnly));
    EXPECT_EQ(sentinel.readAll(), "keep");
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

TEST(CutService, ExactFractionalBoundariesKeepEveryFrameInsideTheInterval)
{
    const auto ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    if (ffmpeg.isEmpty()) GTEST_SKIP() << "FFmpeg unavailable";
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto source = directory.filePath("fractional.mkv");
    QProcess generator;
    generator.start(ffmpeg, {"-hide_banner", "-loglevel", "error", "-f", "lavfi", "-i",
        "testsrc2=size=160x90:rate=30:duration=3", "-c:v", "ffv1", source});
    ASSERT_TRUE(generator.waitForFinished(30000));
    ASSERT_EQ(generator.exitCode(), 0) << generator.readAllStandardError().toStdString();
    pfservices::CutService cutter(ffmpeg.toStdString());
    for (const auto& interval : {std::pair{0.7, 1.7}, std::pair{0.715, 1.715},
                               std::pair{1.715, 2.715}, std::pair{0.715, 0.75}}) {
        const auto [start, end] = interval;
        for (const bool normalize : {false, true}) {
        pfservices::CutRequest request;
        request.inputPath = source.toStdWString();
        request.outputPath = directory.filePath(QString("cut-%1-%2-%3.mp4")
            .arg(start).arg(end).arg(normalize)).toStdWString();
        request.startSeconds = start;
        request.endSeconds = end;
        if (normalize) {
            request.canvasWidth = 160;
            request.canvasHeight = 90;
            request.outputFrameRate = 30;
            request.ensureStereoAudio = true;
            request.sourceHasAudio = false;
        }
        pfcore::VideoDecoder original;
        original.open(source.toStdString());
        pfcore::DecodedFrame frame;
        int expected = 0;
        while (original.readNext(frame, false))
            if (frame.timestampSeconds >= start && frame.timestampSeconds < end) ++expected;
        ASSERT_GT(expected, 0);
        const auto cut = cutter.cut(request);
        ASSERT_TRUE(cut.success) << cut.error;
        pfcore::VideoDecoder output;
        output.open(request.outputPath.string());
        int actual = 0;
        while (output.readNext(frame, false)) ++actual;
        EXPECT_EQ(actual, expected) << "start=" << start << " normalized=" << normalize;
        }
    }
    pfservices::CutRequest earlier, later;
    earlier.inputPath = later.inputPath = source.toStdWString();
    earlier.startSeconds = 0.715; earlier.endSeconds = 1.715;
    later.startSeconds = 1.715; later.endSeconds = 2.715;
    const auto montagePath = directory.filePath("combined.mp4");
    const auto montage = pfservices::exportChronologicalMontage({later, earlier},
        montagePath.toStdWString(), {}, {}, ffmpeg.toStdString(), true);
    ASSERT_TRUE(montage.success) << montage.error;
    pfcore::VideoDecoder combined;
    combined.open(montagePath.toStdString());
    pfcore::DecodedFrame frame;
    int combinedFrames = 0;
    double firstTimestamp=-1, secondClipTimestamp=-1;
    while (combined.readNext(frame, false)) {
        if(combinedFrames==0)firstTimestamp=frame.timestampSeconds;
        if(combinedFrames==30)secondClipTimestamp=frame.timestampSeconds;
        ++combinedFrames;
    }
    EXPECT_EQ(combinedFrames, 60);
    EXPECT_NEAR(secondClipTimestamp-firstTimestamp,1.0,0.00001);
    EXPECT_TRUE(combined.info().hasAudio); // silent sources receive bounded silence
}

TEST(CutService, ExactLosslessPreservesDecodedPixelsAndSamples)
{
    const auto ffmpeg=QStandardPaths::findExecutable("ffmpeg"); ASSERT_FALSE(ffmpeg.isEmpty());
    QTemporaryDir directory; ASSERT_TRUE(directory.isValid());
    const auto run=[&](QStringList args) {
        QProcess process; process.start(ffmpeg,QStringList{"-v","error"}+args);
        EXPECT_TRUE(process.waitForFinished(30000));
        EXPECT_EQ(process.exitCode(),0) << process.readAllStandardError().toStdString();
        return process.readAllStandardOutput();
    };
    for(const auto& format:{QString("yuv420p"),QString("yuv420p10le")}) {
        const auto source=directory.filePath(format+".mkv");
        run({"-f","lavfi","-i","testsrc2=s=160x90:r=30:d=3,format="+format,
            "-f","lavfi","-i","sine=frequency=997:sample_rate=48000:duration=3",
            "-c:v","ffv1","-c:a","pcm_f64le",source});
        pfservices::CutRequest request; request.inputPath=source.toStdWString();
        request.outputPath=directory.filePath(format+".mov").toStdWString();
        request.startSeconds=.715;request.endSeconds=1.715;
        request.mode=pfservices::CutMode::ExactLossless;
        const auto result=pfservices::CutService(ffmpeg.toStdString()).cut(request);
        ASSERT_TRUE(result.success) << result.error;
        const auto hashes=[&](const QString& path,bool trim,double start=.715) {
            QStringList options{"-i",path};
            if(trim)options << "-vf" << QString("trim=start=%1:end=%2,setpts=PTS-STARTPTS").arg(start).arg(start+1);
            options << "-an" << "-pix_fmt" << format << "-fps_mode" << "passthrough" << "-f" << "framemd5" << "-";
            QStringList values;for(const auto& line:run(options).split('\n'))
                if(!line.isEmpty()&&!line.startsWith('#'))values << QString::fromUtf8(line).section(',',-1).trimmed();
            return values;
        };
        const auto expected=hashes(source,true);ASSERT_EQ(expected.size(),30);
        const auto output=QString::fromStdWString(request.outputPath.wstring());
        EXPECT_EQ(hashes(output,false),expected);
        const auto expectedAudio=run({"-i",source,"-af","atrim=start=0.715:end=1.715",
            "-vn","-c:a","pcm_f64le","-f","f64le","-"});
        EXPECT_EQ(run({"-i",output,"-vn","-c:a","pcm_f64le","-f","f64le","-"}),expectedAudio);
        auto later=request;later.startSeconds=1.715;later.endSeconds=2.715;
        const auto montagePath=directory.filePath(format+"-combined.mov");
        const auto montage=pfservices::exportChronologicalMontage({request,later},montagePath.toStdWString(),{},{},ffmpeg.toStdString(),true);
        ASSERT_TRUE(montage.success) << montage.error;
        EXPECT_EQ(hashes(montagePath,false),expected+hashes(source,true,1.715));
        EXPECT_EQ(run({"-i",montagePath,"-vn","-c:a","pcm_f64le","-f","f64le","-"}),
            run({"-i",source,"-af","atrim=start=0.715:end=2.715","-vn","-c:a","pcm_f64le","-f","f64le","-"}));
        pfcore::VideoDecoder decoder;decoder.open(montagePath.toStdString());
        pfcore::DecodedFrame frame;int count=0;double first=-1,second=-1;
        while(decoder.readNext(frame,false)){if(count==0)first=frame.timestampSeconds;if(count==30)second=frame.timestampSeconds;++count;}
        EXPECT_EQ(count,60);EXPECT_NEAR(second-first,1.0,.00001);
        request.outputPath=directory.filePath("unsupported.mp4").toStdWString();
        EXPECT_FALSE(pfservices::CutService(ffmpeg.toStdString()).cut(request).success);
        EXPECT_FALSE(std::filesystem::exists(request.outputPath));
    }
}

TEST(CutService, ExactLosslessRealSourcePixelsAudioAndMontage)
{
    const auto source=qEnvironmentVariable("PF_TEST_EXACT_LOSSLESS_SOURCE");
    if(source.isEmpty())GTEST_SKIP() << "Opt-in exact lossless real-source verification";
    const auto ffmpeg=QStandardPaths::findExecutable("ffmpeg");ASSERT_FALSE(ffmpeg.isEmpty());
    QTemporaryDir directory;ASSERT_TRUE(directory.isValid());
    bool valid=false;double start=qEnvironmentVariable("PF_TEST_EXACT_LOSSLESS_START").toDouble(&valid);
    if(!valid)start=.715;
    const auto run=[&](QStringList args) {
        QProcess process;process.start(ffmpeg,QStringList{"-v","error","-threads","4"}+args);
        EXPECT_TRUE(process.waitForFinished(90000));
        EXPECT_EQ(process.exitCode(),0) << process.readAllStandardError().toStdString();
        return process.readAllStandardOutput();
    };
    pfcore::VideoDecoder original;original.open(source.toStdString());
    const bool hasAudio=original.info().hasAudio;
    const auto videoMetadata=[&](const QString& path) {
        QProcess process;process.start(QStandardPaths::findExecutable("ffprobe"),
            {"-v","error","-select_streams","v:0","-show_entries",
             "stream=pix_fmt,color_range,color_space,color_transfer,color_primaries,sample_aspect_ratio",
             "-of","json",path});
        EXPECT_TRUE(process.waitForFinished(30000));EXPECT_EQ(process.exitCode(),0);
        return QJsonDocument::fromJson(process.readAllStandardOutput()).object()["streams"].toArray().first().toObject();
    };
    const auto sourceMetadata=videoMetadata(source);
    const auto checkMetadata=[&](const QString& path) {
        const auto actual=videoMetadata(path);
        for(const auto* key:{"pix_fmt","color_range","color_space","color_transfer","color_primaries","sample_aspect_ratio"}) {
            if(sourceMetadata.contains(key)){EXPECT_EQ(actual[key],sourceMetadata[key]) << key;}
        }
    };
    const auto decode=[&](const QString& path,bool input,bool audio,double offset) {
        QStringList args;
        if(input)args << "-copyts" << "-ss" << QString::number(std::max(0.,offset-.5),'f',9)
            << "-t" << "1.75";
        args << "-i" << path;
        if(input)args << (audio?"-af":"-vf") << QString("%1trim=start=%2:end=%3,%1setpts=PTS-STARTPTS")
            .arg(audio?"a":"").arg(offset,0,'f',9).arg(offset+1,0,'f',9);
        if(audio)args << "-map" << "0:a:0" << "-vn" << "-c:a" << "pcm_f64le" << "-f" << "f64le" << "-";
        else args << "-map" << "0:v:0" << "-an" << "-fps_mode" << "passthrough" << "-f" << "framemd5" << "-";
        return run(args);
    };
    const auto hashes=[](const QByteArray& data){
        QStringList result;
        for(const auto& line:data.split('\n')) {
            if(!line.isEmpty()&&!line.startsWith('#'))
                result << QString::fromUtf8(line).section(',',-1).trimmed();
        }
        return result;
    };
    QStringList expectedVideo;QByteArray expectedAudio;
    std::vector<pfservices::CutRequest> jobs;
    pfservices::CutService cutter(ffmpeg.toStdString());
    for(int index=0;index<2;++index) {
        pfservices::CutRequest request;request.inputPath=source.toStdWString();
        request.outputPath=directory.filePath(QString::number(index)+".mov").toStdWString();
        request.mode=pfservices::CutMode::ExactLossless;
        request.startSeconds=start+index;request.endSeconds=start+index+1;
        const auto expected=hashes(decode(source,true,false,request.startSeconds));ASSERT_FALSE(expected.isEmpty());
        expectedVideo+=expected;
        const auto cut=cutter.cut(request);ASSERT_TRUE(cut.success) << cut.error;
        const auto output=QString::fromStdWString(request.outputPath.wstring());
        checkMetadata(output);
        EXPECT_EQ(hashes(decode(output,false,false,0)),expected);
        if(hasAudio) {
            const auto samples=decode(source,true,true,request.startSeconds);ASSERT_FALSE(samples.isEmpty());
            EXPECT_EQ(decode(output,false,true,0),samples);expectedAudio+=samples;
        }
        jobs.push_back(request);
    }
    const auto montagePath=directory.filePath("combined.mov");
    const auto montage=pfservices::exportChronologicalMontage(jobs,montagePath.toStdWString(),{},{},ffmpeg.toStdString(),true);
    ASSERT_TRUE(montage.success) << montage.error;
    checkMetadata(montagePath);
    EXPECT_EQ(hashes(decode(montagePath,false,false,0)),expectedVideo);
    if(hasAudio){EXPECT_EQ(decode(montagePath,false,true,0),expectedAudio);}
    pfcore::VideoDecoder combined;combined.open(montagePath.toStdString());
    EXPECT_EQ(combined.info().hasAudio,hasAudio);
    EXPECT_EQ(combined.info().width,original.info().width);
    EXPECT_EQ(combined.info().height,original.info().height);
}

TEST(CutService, ExactLosslessVariableFrameTimesSurviveTrimAndMontage)
{
    const auto ffmpeg=QStandardPaths::findExecutable("ffmpeg");ASSERT_FALSE(ffmpeg.isEmpty());
    QTemporaryDir directory;ASSERT_TRUE(directory.isValid());
    const auto source=directory.filePath("variable.mkv");
    QProcess generate;generate.start(ffmpeg,{"-v","error","-f","lavfi","-i",
        "testsrc2=s=160x90:r=30:d=4","-vf","select=not(eq(mod(n\\,7)\\,1))*not(eq(mod(n\\,7)\\,2))",
        "-fps_mode","passthrough","-c:v","ffv1",source});
    ASSERT_TRUE(generate.waitForFinished(30000));ASSERT_EQ(generate.exitCode(),0) << generate.readAllStandardError().toStdString();
    const auto times=[&](const QString& path) {
        QProcess probe;probe.start(QStandardPaths::findExecutable("ffprobe"),{"-v","error","-select_streams","v:0",
            "-show_frames","-show_entries","frame=best_effort_timestamp_time","-of","json",path});
        EXPECT_TRUE(probe.waitForFinished(30000));EXPECT_EQ(probe.exitCode(),0);
        std::vector<double> result;
        for(const auto frame:QJsonDocument::fromJson(probe.readAllStandardOutput()).object()["frames"].toArray())
            result.push_back(frame.toObject()["best_effort_timestamp_time"].toString().toDouble());
        return result;
    };
    const auto sourceTimes=times(source);
    std::vector<double> combinedExpected;std::vector<pfservices::CutRequest> jobs;
    pfservices::CutService cutter(ffmpeg.toStdString());
    for(int index=0;index<2;++index) {
        pfservices::CutRequest request;request.mode=pfservices::CutMode::ExactLossless;
        request.inputPath=source.toStdWString();request.outputPath=directory.filePath(QString::number(index)+".mov").toStdWString();
        request.startSeconds=.715+index;request.endSeconds=1.715+index;
        std::vector<double> expected;
        for(const double t:sourceTimes)if(t>=request.startSeconds&&t<request.endSeconds){
            expected.push_back(t-request.startSeconds);combinedExpected.push_back(t-.715);
        }
        const auto result=cutter.cut(request);ASSERT_TRUE(result.success) << result.error;
        const auto actual=times(QString::fromStdWString(request.outputPath.wstring()));
        ASSERT_EQ(actual.size(),expected.size());
        for(std::size_t i=0;i<actual.size();++i)EXPECT_NEAR(actual[i],expected[i],.000002);
        jobs.push_back(request);
    }
    const auto output=directory.filePath("combined.mov");
    const auto montage=pfservices::exportChronologicalMontage(jobs,output.toStdWString(),{},{},ffmpeg.toStdString(),true);
    ASSERT_TRUE(montage.success) << montage.error;
    const auto actual=times(output);ASSERT_EQ(actual.size(),combinedExpected.size());
    for(std::size_t i=0;i<actual.size();++i)EXPECT_NEAR(actual[i],combinedExpected[i],.000002);
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

TEST(ExportQueue, PreparesEachClipOnlyAfterThePreviousClipIsWritten)
{
    const auto ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    ASSERT_FALSE(ffmpeg.isEmpty());
    QTemporaryDir directory; ASSERT_TRUE(directory.isValid());
    std::vector<pfservices::CutRequest> jobs(3);
    int prepared = 0;
    for (std::size_t i = 0; i < jobs.size(); ++i) {
        auto& request = jobs[i];
        request.inputPath = std::filesystem::path(PF_TEST_FIXTURE_DIR) / "tiny.mp4";
        request.outputPath = directory.filePath(QString::number(i) + ".mp4").toStdWString();
        request.endSeconds = 1;
        request.prepare = [&, i](pfservices::CutRequest& clip) {
            EXPECT_EQ(prepared++, static_cast<int>(i));
            if (i) { EXPECT_TRUE(std::filesystem::exists(jobs[i - 1].outputPath)); }
            EXPECT_FALSE(std::filesystem::exists(jobs.back().outputPath));
            clip.endSeconds = .2;
            clip.progress = [](double seconds) { EXPECT_LE(seconds, .2); };
        };
    }
    const auto result = pfservices::runExportQueue(jobs, {}, {}, ffmpeg.toStdString());
    ASSERT_EQ(result.completed.size(), jobs.size());
    for (const auto& clip : result.completed) ASSERT_TRUE(clip.success) << clip.error;
    EXPECT_EQ(prepared, 3);
}

TEST(CutService, CancellationDuringPreparationNeverStartsEncoder)
{
    QTemporaryDir directory; ASSERT_TRUE(directory.isValid());
    std::stop_source stop;
    pfservices::CutRequest request;
    request.inputPath = std::filesystem::path(PF_TEST_FIXTURE_DIR) / "tiny.mp4";
    request.outputPath = directory.filePath("cancelled.mp4").toStdWString();
    request.endSeconds = 1;
    request.stopToken = stop.get_token();
    request.prepare = [&](pfservices::CutRequest&) { stop.request_stop(); };
    const auto result = pfservices::CutService("nonexistent-ffmpeg").cut(request);
    EXPECT_TRUE(result.cancelled);
    EXPECT_TRUE(result.executable.empty());
    EXPECT_FALSE(std::filesystem::exists(request.outputPath));
}

TEST(ExportQueue, ReusesIdenticalCutsButKeepsSeparateNumberedFiles)
{
    const auto ffmpeg = QStandardPaths::findExecutable("ffmpeg"); ASSERT_FALSE(ffmpeg.isEmpty());
    QTemporaryDir directory; ASSERT_TRUE(directory.isValid());
    std::vector<pfservices::CutRequest> jobs(3);
    for (std::size_t i = 0; i < jobs.size(); ++i) {
        jobs[i].inputPath = std::filesystem::path(PF_TEST_FIXTURE_DIR) / "tiny.mp4";
        jobs[i].outputPath = directory.filePath(QString::number(i) + ".mp4").toStdWString();
        jobs[i].endSeconds = .2;
    }
    jobs[2].quality = 22;
    const auto result = pfservices::runExportQueue(jobs, {}, {}, ffmpeg.toStdString());
    ASSERT_EQ(result.completed.size(), 3u);
    for (const auto& cut : result.completed) ASSERT_TRUE(cut.success) << cut.error;
    EXPECT_FALSE(result.completed[0].reused); EXPECT_TRUE(result.completed[1].reused); EXPECT_FALSE(result.completed[2].reused);
    QFile first(QString::fromStdWString(jobs[0].outputPath.wstring()));
    QFile second(QString::fromStdWString(jobs[1].outputPath.wstring()));
    ASSERT_TRUE(first.open(QIODevice::ReadOnly)); ASSERT_TRUE(second.open(QIODevice::ReadOnly));
    EXPECT_EQ(first.readAll(), second.readAll());
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
            const auto temporaries = QDir(directory.path()).entryInfoList({"cancelled.part-*.mp4"}, QDir::Files);
            if (std::any_of(temporaries.begin(), temporaries.end(), [](const auto& file) { return file.size() > 0; })) {
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
    EXPECT_TRUE(QDir(directory.path()).entryList({"cancelled.part-*.mp4"}, QDir::Files).isEmpty());
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

TEST(CutService, ExplicitQualityAndBitrateAreAppliedToEveryEncoder)
{
    const auto has = [](const std::vector<std::string>& args, const std::string& flag, const std::string& value) {
        const auto it = std::find(args.begin(), args.end(), flag);
        return it != args.end() && std::next(it) != args.end() && *std::next(it) == value;
    };
    for (const std::string encoder : {"libx264", "h264_nvenc", "h264_amf", "h264_qsv"}) {
        const auto quality = pfservices::CutService::videoEncodingArguments(encoder, 14, 0, 8);
        const auto bitrate = pfservices::CutService::videoEncodingArguments(encoder, 14, 30000, 8);
        EXPECT_TRUE(has(bitrate, "-b:v", "30000k")) << encoder;
        if (encoder == "libx264") { EXPECT_TRUE(has(quality, "-crf", "14")); }
        if (encoder == "h264_nvenc") {
            EXPECT_TRUE(has(quality, "-cq", "14"));
            EXPECT_TRUE(has(quality, "-b:v", "0"));
            EXPECT_FALSE(has(bitrate, "-cq", "14"));
        }
        if (encoder == "h264_amf") {
            EXPECT_TRUE(has(quality, "-rc", "cqp"));
            EXPECT_TRUE(has(quality, "-qp_i", "14"));
            EXPECT_TRUE(has(quality, "-qp_p", "14"));
            EXPECT_TRUE(has(quality, "-qp_b", "14"));
        }
        if (encoder == "h264_qsv") { EXPECT_TRUE(has(quality, "-global_quality", "14")); }
        EXPECT_EQ(std::count(bitrate.begin(), bitrate.end(), "-b:v"), 1);
    }
}

TEST(CutService, SelectedQualityAndBitrateProduceDecodableClipsWithoutResizing)
{
    const auto ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    if (ffmpeg.isEmpty()) GTEST_SKIP();
    QTemporaryDir folder;
    pfservices::CutRequest request;
    request.inputPath = std::filesystem::path(PF_TEST_FIXTURE_DIR) / "tiny.mp4";
    request.outputPath = folder.filePath("quality.mp4").toStdWString();
    request.endSeconds = 0.5;
    request.quality = 14;
    pfservices::CutService cutter(ffmpeg.toStdString());
    const auto quality = cutter.cut(request);
    ASSERT_TRUE(quality.success) << quality.error;
    pfcore::VideoDecoder original; original.open(request.inputPath.string());
    pfcore::VideoDecoder high; high.open(request.outputPath.string());
    EXPECT_EQ(high.info().width, original.info().width);
    EXPECT_EQ(high.info().height, original.info().height);
    pfcore::DecodedFrame frame;
    EXPECT_TRUE(high.readNext(frame));
    request.outputPath = folder.filePath("bitrate.mp4").toStdWString();
    request.videoBitrateKbps = 30000;
    const auto bitrate = cutter.cut(request);
    ASSERT_TRUE(bitrate.success) << bitrate.error;
    const auto flag = std::find(bitrate.arguments.begin(), bitrate.arguments.end(), "-b:v");
    ASSERT_NE(flag, bitrate.arguments.end()); EXPECT_EQ(*std::next(flag), "30000k");
    pfcore::VideoDecoder custom; custom.open(request.outputPath.string());
    EXPECT_EQ(custom.info().width, original.info().width);
    EXPECT_TRUE(custom.readNext(frame));
    request.outputPath = folder.filePath("copy.mp4").toStdWString(); request.mode = pfservices::CutMode::Fast;
    const auto copied = cutter.cut(request);
    ASSERT_TRUE(copied.success) << copied.error;
    EXPECT_EQ(std::find(copied.arguments.begin(), copied.arguments.end(), "-b:v"), copied.arguments.end());
    request.quality = 52; EXPECT_FALSE(cutter.cut(request).success);
    request.quality = 18; request.videoBitrateKbps = 500001; EXPECT_FALSE(cutter.cut(request).success);
}

TEST(MontageExport, CanvasChecksAspectRatioOverflowAndRotation) {
    pfcore::VideoInfo info;
    info.width = 1920; info.height = 1080; info.sampleAspectRatio = 1.5;
    const auto normal = pfservices::detail::montageCanvas(info);
    ASSERT_TRUE(normal);
    EXPECT_EQ(*normal, (std::pair<int,int>{2880,1080}));
    info.rotationDegrees = 90;
    EXPECT_EQ(*pfservices::detail::montageCanvas(info), (std::pair<int,int>{1080,2880}));
    info.sampleAspectRatio = 1e12;
    EXPECT_FALSE(pfservices::detail::montageCanvas(info));
    info.sampleAspectRatio = std::numeric_limits<double>::infinity();
    EXPECT_FALSE(pfservices::detail::montageCanvas(info));
}

TEST(ExportQueue, ExistingNamesAndCancelledReservationsPreserveAllPreviousFiles)
{
    const auto ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    ASSERT_FALSE(ffmpeg.isEmpty());
    QTemporaryDir directory; ASSERT_TRUE(directory.isValid());
    for (const auto& name : {"clip.mp4", "clip (1).mp4"}) {
        QFile old(directory.filePath(name)); ASSERT_TRUE(old.open(QIODevice::WriteOnly)); old.write("original");
    }
    pfservices::CutRequest request;
    request.inputPath = std::filesystem::path(PF_TEST_FIXTURE_DIR) / "tiny.mp4";
    request.outputPath = directory.filePath("clip.mp4").toStdWString(); request.endSeconds = .5;
    const auto batch = pfservices::runExportQueue({request, request}, {}, {}, ffmpeg.toStdString());
    ASSERT_EQ(batch.completed.size(), 2u);
    for (const auto& result : batch.completed) ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(batch.completed[0].outputPath, std::filesystem::path(directory.filePath("clip (2).mp4").toStdWString()));
    EXPECT_EQ(batch.completed[1].outputPath, std::filesystem::path(directory.filePath("clip (3).mp4").toStdWString()));
    EXPECT_TRUE(batch.completed[1].reused);
    for (const auto& name : {"clip.mp4", "clip (1).mp4"}) {
        QFile old(directory.filePath(name)); ASSERT_TRUE(old.open(QIODevice::ReadOnly)); EXPECT_EQ(old.readAll(), "original");
    }
    request.inputPath = directory.filePath("missing.mp4").toStdWString();
    const auto failed = pfservices::runExportQueue({request}, {}, {}, ffmpeg.toStdString());
    ASSERT_EQ(failed.completed.size(), 1u); EXPECT_FALSE(failed.completed[0].success);
    EXPECT_FALSE(QFileInfo::exists(directory.filePath("clip (4).mp4")));
    std::stop_source stop; stop.request_stop();
    EXPECT_TRUE(pfservices::runExportQueue({request}, stop.get_token(), {}, ffmpeg.toStdString()).cancelled);
    EXPECT_FALSE(QFileInfo::exists(directory.filePath("clip (4).mp4")));
}

TEST(MontageExport, StreamCopyReorderedFramesMatchSourceAndHaveIncreasingTimestamps)
{
    const auto ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    ASSERT_FALSE(ffmpeg.isEmpty());
    QTemporaryDir directory; ASSERT_TRUE(directory.isValid());
    const auto realSource = qEnvironmentVariable("PF_TEST_LOSSLESS_SOURCE");
    for (const auto& encoder : {QString("libx264"), QString("libx265")}) {
        const bool real = !realSource.isEmpty();
        if (real && encoder != "libx265") continue;
        const auto source = real ? realSource : directory.filePath(encoder + ".mp4");
        if (!real) {
            QProcess generate;
            QStringList args{"-v", "error", "-f", "lavfi", "-i", "testsrc2=s=192x108:r=24:d=16",
                "-c:v", encoder, "-threads", "2", "-g", "96", "-bf", "8"};
            if (encoder == "libx265") args << "-x265-params" << "pools=2:frame-threads=2:open-gop=1:scenecut=0:log-level=error" << "-pix_fmt" << "yuv420p10le";
            args << source;
            generate.start(ffmpeg, args); ASSERT_TRUE(generate.waitForFinished(30000));
            ASSERT_EQ(generate.exitCode(), 0) << generate.readAllStandardError().toStdString();
        }
        const auto hashes = [&](const QString& path, QStringList inputOptions = {}) {
            QProcess decode;
            QStringList args{"-v", "error", "-threads", "4"}; args << inputOptions << "-i" << path;
            if (real && path == source) args << "-t" << "14";
            args << "-map" << "0:v:0" << "-fps_mode" << "passthrough" << "-f" << "framemd5" << "-";
            decode.start(ffmpeg, args);
            EXPECT_TRUE(decode.waitForFinished(120000)); EXPECT_EQ(decode.exitCode(), 0);
            const auto errors = decode.readAllStandardError();
            EXPECT_TRUE(errors.isEmpty()) << path.toStdString() << ": " << errors.toStdString();
            QList<QByteArray> frames;
            qint64 previous = -1;
            for (const auto& line : decode.readAllStandardOutput().split('\n')) {
                if (line.isEmpty() || line.startsWith('#')) continue;
                const auto fields = line.split(',');
                const auto timestamp = fields[2].trimmed().toLongLong();
                EXPECT_GT(timestamp, previous) << "overlapping presentation timestamps";
                previous = timestamp; frames.push_back(fields.last().trimmed());
            }
            return frames;
        };
        const double base = real ? 1800.5 : 2.5;
        pfservices::CutRequest a, b;
        a.inputPath = b.inputPath = source.toStdWString(); a.mode = b.mode = pfservices::CutMode::Fast;
        a.startSeconds = base; a.endSeconds = base + (real ? 5 : 3.6);
        b.startSeconds = real ? 2132.4 : 10.4; b.endSeconds = b.startSeconds + (real ? 5 : 3.1);
        a.outputPath = directory.filePath(encoder + "-a.mp4").toStdWString();
        b.outputPath = directory.filePath(encoder + "-b.mp4").toStdWString();
        pfservices::CutService cutter(ffmpeg.toStdString());
        for (const auto& request : {a, b}) {
            const auto cut = cutter.cut(request); ASSERT_TRUE(cut.success) << cut.error;
            const auto prepared = QString::fromStdWString(request.outputPath.wstring()) + ".splice.mp4";
            const double singlePassSpan = pfcore::prepareStreamCopySplice(
                QString::fromStdWString(request.outputPath.wstring()).toStdString(), prepared.toStdString());
            EXPECT_NEAR(singlePassSpan,pfcore::mediaPacketDuration(prepared.toStdString()),.00001);
        }
        const auto aHashes = hashes(QString::fromStdWString(a.outputPath.wstring()));
        const auto bHashes = hashes(QString::fromStdWString(b.outputPath.wstring()));
        ASSERT_FALSE(aHashes.isEmpty()); ASSERT_FALSE(bHashes.isEmpty());
        const auto originalA = hashes(source, real ? QStringList{"-ss", "1793"} : QStringList{});
        const auto originalB = real ? hashes(source, {"-ss", "2127"}) : originalA;
        for (const auto& hash : aHashes) EXPECT_TRUE(originalA.contains(hash)) << "modified or damaged decoded source frame";
        for (const auto& hash : bHashes) EXPECT_TRUE(originalB.contains(hash)) << "modified or damaged decoded source frame";
        const auto output = directory.filePath(encoder + "-montage.mp4");
        { QFile previous(output); ASSERT_TRUE(previous.open(QIODevice::WriteOnly)); previous.write("keep previous montage"); }
        const auto result = pfservices::exportChronologicalMontage({b, a}, output.toStdWString(), {}, {}, ffmpeg.toStdString());
        ASSERT_TRUE(result.success) << result.error;
        EXPECT_EQ(result.outputPath, std::filesystem::path(directory.filePath(encoder + "-montage (1).mp4").toStdWString()));
        QFile previous(output); ASSERT_TRUE(previous.open(QIODevice::ReadOnly)); EXPECT_EQ(previous.readAll(), "keep previous montage");
        const auto expected = aHashes + bHashes;
        const auto actual = hashes(QString::fromStdWString(result.outputPath.wstring()));
        EXPECT_EQ(actual.size(), expected.size()) << encoder.toStdString();
        for (qsizetype index = 0; index < std::min(actual.size(), expected.size()); ++index) {
            if (actual[index] != expected[index]) {
                ADD_FAILURE() << encoder.toStdString() << " first different frame " << index
                    << ", clip A=" << aHashes.size() << ", clip B=" << bHashes.size()
                    << ", actual=" << actual[index].toStdString() << ", expected=" << expected[index].toStdString();
                break;
            }
        }
        // The UI supplies movement-class order, which must survive the
        // montage stage even when it contradicts the source chronology.
        const auto groupedOutput = directory.filePath(encoder + "-grouped-montage.mp4");
        const auto grouped = pfservices::exportChronologicalMontage({b, a},
            groupedOutput.toStdWString(), {}, {}, ffmpeg.toStdString(), true);
        ASSERT_TRUE(grouped.success) << grouped.error;
        EXPECT_EQ(hashes(QString::fromStdWString(grouped.outputPath.wstring())), bHashes + aHashes);
    }
}
