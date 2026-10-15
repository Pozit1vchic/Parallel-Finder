#include "../../services/src/ExportConcurrency.hpp"
#include <pfservices/ExportQueue.hpp>
#include <pfservices/MontageExport.hpp>
#include <gtest/gtest.h>
#include <QElapsedTimer>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QFile>
#include <atomic>
#include <iostream>

TEST(ExportConcurrency, BudgetsCpuMemoryResolutionAndUnknownMemory)
{
    using pfservices::detail::exportBudget;
    EXPECT_EQ(exportBudget(100, 32, 32ull<<30, 3840ull*2160).workers, 4u);
    EXPECT_EQ(exportBudget(100, 2, 32ull<<30, 1920ull*1080).workers, 1u);
    EXPECT_EQ(exportBudget(100, 32, 0, 1920ull*1080).workers, 1u);
    EXPECT_EQ(exportBudget(100, 32, 1ull<<30, 3840ull*2160).workers, 1u);
    EXPECT_EQ(exportBudget(2, 32, 32ull<<30, 1920ull*1080).workers, 2u);
    EXPECT_LE(exportBudget(100, 128, 128ull<<30, 7680ull*4320).threads, 8u);
    EXPECT_EQ(exportBudget(100, 128, 128ull<<30, std::numeric_limits<std::uint64_t>::max()).workers, 1u);
}

// Opt-in real encoder benchmark: deterministic motion, independently readable
// files, retained input order and lossless decoded frame equality.
TEST(ExportConcurrency, RealAllModesKeepOrderAndLosslessPixels)
{
    if (qEnvironmentVariableIsEmpty("PF_TEST_EXPORT_PARALLEL")) GTEST_SKIP() << "set PF_TEST_EXPORT_PARALLEL=1";
    const auto ffmpeg = QStandardPaths::findExecutable("ffmpeg");
    ASSERT_FALSE(ffmpeg.isEmpty());
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto source = directory.filePath("source.mp4");
    QProcess generator;
    generator.start(ffmpeg, {"-v", "error", "-f", "lavfi", "-i", "testsrc2=size=1280x720:rate=25:duration=10", "-c:v", "libx264", "-preset", "ultrafast", "-crf", "18", "-y", source});
    ASSERT_TRUE(generator.waitForFinished(60000));
    ASSERT_EQ(generator.exitCode(), 0) << generator.readAllStandardError().toStdString();
    const auto frames = [&](const QString& path, double start, double end) {
        QProcess decode;
        QStringList arguments{"-v", "error", "-i", path};
        if (end > start) arguments << "-vf" << QString("trim=start=%1:end=%2,setpts=PTS-STARTPTS").arg(start).arg(end);
        arguments << "-f" << "hash" << "-hash" << "sha256" << "-";
        decode.start(ffmpeg, arguments);
        if (!decode.waitForFinished(60000) || decode.exitCode()) return QByteArray{};
        return decode.readAllStandardOutput();
    };
    for (const auto mode : {pfservices::CutMode::Fast, pfservices::CutMode::Exact, pfservices::CutMode::ExactLossless}) {
        std::vector<pfservices::CutRequest> jobs;
        std::atomic<unsigned> callbacks{0};
        for (int i=0;i<8;++i) {
            pfservices::CutRequest request;
            request.inputPath = source.toStdWString();
            request.outputPath = directory.filePath(QString("%1-%2.%3").arg(static_cast<int>(mode)).arg(i).arg(mode==pfservices::CutMode::ExactLossless?"mov":"mp4")).toStdWString();
            request.startSeconds = i; request.endSeconds = i+0.8; request.mode = mode;
            request.progress = [&](double) {
                EXPECT_EQ(callbacks.fetch_add(1), 0u);
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                EXPECT_EQ(callbacks.fetch_sub(1), 1u);
            };
            jobs.push_back(request);
        }
        QElapsedTimer clock; clock.start();
        const pfservices::CutService cutter(ffmpeg.toStdString());
        for (auto request : jobs) {
            request.outputPath += L".serial" + request.outputPath.extension().wstring();
            ASSERT_TRUE(cutter.cut(request).success);
        }
        const auto serialMs = clock.elapsed(); clock.restart();
        std::size_t reported=0;
        const auto batch=pfservices::runExportQueue(jobs, {}, [&](auto completed, auto total) { EXPECT_EQ(total, 8); EXPECT_EQ(completed, ++reported); }, ffmpeg.toStdString());
        const auto parallelMs=clock.elapsed();
        ASSERT_EQ(batch.completed.size(), jobs.size());
        for (std::size_t i=0;i<jobs.size();++i) {
            ASSERT_TRUE(batch.completed[i].success) << batch.completed[i].error;
            EXPECT_EQ(batch.completed[i].outputPath,jobs[i].outputPath);
            if(mode==pfservices::CutMode::ExactLossless) {
                const auto outputHash=frames(QString::fromStdWString(batch.completed[i].outputPath.wstring()),0,0);
                ASSERT_FALSE(outputHash.isEmpty());
                EXPECT_EQ(outputHash,frames(source,jobs[i].startSeconds,jobs[i].endSeconds));
            }
        }
        std::cout << "export mode=" << static_cast<int>(mode) << " serial_ms=" << serialMs << " parallel_ms=" << parallelMs << '\n';
        const auto output=directory.filePath(QString("montage-%1.%2").arg(static_cast<int>(mode)).arg(mode==pfservices::CutMode::ExactLossless?"mov":"mp4"));
        const auto montage=pfservices::exportChronologicalMontage(jobs,output.toStdWString(),{},{},ffmpeg.toStdString(),true);
        EXPECT_TRUE(montage.success) << montage.error;
        EXPECT_TRUE(QFile::exists(output));
    }
}
