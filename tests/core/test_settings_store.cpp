#include <gtest/gtest.h>

#include <pfservices/SettingsStore.hpp>

#include <QDir>
#include <QTemporaryDir>
#include <QUrl>
#include <QFile>

#include <filesystem>
#include <atomic>
#include <stdexcept>
#include <thread>
#include <mutex>

namespace {
TEST(SettingsStore, SuccessfulOperationsClearPreviousErrors)
{
    QTemporaryDir directory;ASSERT_TRUE(directory.isValid());
    pfservices::SettingsStore store(directory.filePath("settings.json").toStdString());
    std::string error="previous failure";
    const auto defaults=store.load(error);EXPECT_TRUE(error.empty());
    error="previous failure";ASSERT_TRUE(store.save(defaults,error));EXPECT_TRUE(error.empty());
    error="previous failure";store.load(error);EXPECT_TRUE(error.empty());
}

TEST(SettingsStore, ConcurrentReadersAndAtomicCommitsKeepCompleteSnapshots)
{
    QTemporaryDir directory;ASSERT_TRUE(directory.isValid());
    pfservices::SettingsStore store(directory.filePath("concurrent.json").toStdString());
    pfservices::Settings initial;initial.processingThreads=1;initial.appearance.insert("marker",0);
    std::string error;ASSERT_TRUE(store.save(initial,error))<<error;
    std::atomic<bool> done=false;
    std::atomic<unsigned> failures=0;
    std::mutex diagnosticMutex;std::string firstError;
    const auto fail=[&](const std::string& message) {
        ++failures;std::lock_guard lock(diagnosticMutex);if(firstError.empty())firstError=message;
    };
    std::jthread writer([&] {
        for(int i=1;i<=500;++i) {
            auto next=initial;next.processingThreads=static_cast<std::size_t>(i%16+1);next.appearance.insert("marker",i);
            std::string local;if(!store.save(next,local))fail(local);
        }
        done=true;
    });
    const auto reader=[&] {
        unsigned reads=0;
        do {
            std::string local;const auto snapshot=store.load(local);
            if(!local.empty())fail(local);
            else if(snapshot.processingThreads!=static_cast<std::size_t>(snapshot.appearance.value("marker").toInt(-1)%16+1))
                fail("A reader saw missing or inconsistent snapshot fields");
            ++reads;
        } while(!done || reads<1000);
    };
    std::jthread one(reader),two(reader);
    writer.join();one.join();two.join();
    EXPECT_EQ(failures.load(),0u)<<firstError;
}

TEST(SettingsStore, AllSixInterfaceLanguagesSurviveDiskRoundTrip)
{
    QTemporaryDir directory;ASSERT_TRUE(directory.isValid());
    pfservices::SettingsStore store(directory.filePath("settings.json").toStdString());
    std::string error;
    for(const std::string language:{"ru","en","ky","kk","uk","tr"}) {
        pfservices::Settings settings;settings.language=language;
        ASSERT_TRUE(store.save(settings,error))<<error;
        EXPECT_EQ(store.load(error).language,language);
    }
}
TEST(SettingsStore, McpDefaultsAndMalformedSettingsDoNotEnableRemoteControl)
{
    QTemporaryDir directory;ASSERT_TRUE(directory.isValid());
    pfservices::SettingsStore store(directory.filePath("settings.json").toStdString());std::string error;
    auto settings=store.load(error);EXPECT_FALSE(settings.mcpEnabled);EXPECT_FALSE(settings.mcpAllowControl);EXPECT_EQ(settings.mcpPort,32145);EXPECT_TRUE(settings.mcpToken.empty());
    settings.mcpEnabled=true;settings.mcpAllowControl=true;settings.mcpPort=32456;settings.mcpToken=std::string(64,'a');
    ASSERT_TRUE(store.save(settings,error));const auto restored=store.load(error);
    EXPECT_TRUE(restored.mcpEnabled);EXPECT_TRUE(restored.mcpAllowControl);EXPECT_EQ(restored.mcpPort,32456);EXPECT_EQ(restored.mcpToken,settings.mcpToken);
    QFile malformed(directory.filePath("settings.json"));ASSERT_TRUE(malformed.open(QIODevice::WriteOnly));
    malformed.write(R"({"mcpEnabled":"yes","mcpAllowControl":1,"mcpPort":1234.5,"mcpToken":"short"})");malformed.close();
    const auto safe=store.load(error);EXPECT_FALSE(safe.mcpEnabled);EXPECT_FALSE(safe.mcpAllowControl);EXPECT_EQ(safe.mcpPort,32145);EXPECT_TRUE(safe.mcpToken.empty());
}

TEST(SettingsStore, MigratesCachedFolderUrlWithoutDecodingPlainPaths)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    pfservices::SettingsStore store((directory.path() + "/settings.json").toStdString());
    const QString local = directory.path() + QString::fromUtf8("/кэш с пробелами");
    pfservices::Settings legacy;
    legacy.cachePath = QUrl::fromLocalFile(local).toString(QUrl::FullyEncoded).toStdString();
    std::string error;
    ASSERT_TRUE(store.save(legacy, error));
    EXPECT_EQ(store.load(error).cachePath, local.toStdString());
    legacy.cachePath = (directory.path() + "/literal%20folder").toStdString();
    ASSERT_TRUE(store.save(legacy, error));
    EXPECT_EQ(store.load(error).cachePath, legacy.cachePath);
}

TEST(SettingsStore, MigratesRetiredClipModeToMotion)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    pfservices::SettingsStore store((directory.path() + "/settings.json").toStdString());
    pfservices::Settings legacy;
    legacy.analysisMode = "clips";
    std::string error;
    ASSERT_TRUE(store.save(legacy, error));
    EXPECT_EQ(store.load(error).analysisMode, "motion");
}

TEST(SettingsStore, MissingFileDefaultsToEnglishAndAppearanceRoundTrips)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    pfservices::SettingsStore store((directory.path() + "/settings.json").toStdString());
    std::string error;
    auto settings = store.load(error);
    EXPECT_EQ(settings.language, "en");
    EXPECT_FALSE(settings.expandedSearch);
    settings.expandedSearch = true;
    settings.language = "ru";
    settings.appearance.insert("accentColor", "blue");
    ASSERT_TRUE(store.save(settings, error));
    auto restored = store.load(error);
    EXPECT_EQ(restored.language, "ru");
    EXPECT_TRUE(restored.expandedSearch);
    EXPECT_EQ(restored.appearance.value("accentColor").toString(), "blue");
}

TEST(SettingsStore, RemembersModelChoiceIndependentlyOfDownloadedFile)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    pfservices::SettingsStore store((directory.path() + "/settings.json").toStdString());
    std::string error;
    auto settings = store.load(error);
    EXPECT_TRUE(settings.modelChoice.empty());
    settings.modelPath = "C:/models/yolo26m-pose.onnx";
    settings.modelChoice = "yolo26s-pose.onnx";
    ASSERT_TRUE(store.save(settings, error)) << error;
    const auto restored = store.load(error);
    EXPECT_EQ(restored.modelPath, settings.modelPath);
    EXPECT_EQ(restored.modelChoice, "yolo26s-pose.onnx");
}

TEST(SettingsStore, SavesAndLoadsAtomically)
{
    const auto path = std::filesystem::temp_directory_path() / "parallel-finder-settings-test" / "settings.json";
    std::error_code ignored;
    std::filesystem::remove_all(path.parent_path(), ignored);

    pfservices::SettingsStore store(path.string());
    pfservices::Settings expected;
    expected.provider = "dml";
    expected.cacheLimitBytes = 123456;
    expected.processingThreads = 12;
    expected.sceneThreshold = 42.0;
    expected.sceneMinFrames = 6;
    expected.similarityThreshold = 0.91;
    expected.candidateThreshold = 0.61;
    expected.minRepeatGapSec = 7.0;
    expected.sameFileGapSec = 2.5;
    expected.crossFileGapSec = 1.25;
    expected.duplicateWindowSec = 1.75;
    expected.noiseFactor = 0.8;
    expected.maxUniqueResults = 321;
    expected.timeWeight = 0.35;
    expected.sakoeChibaRatio = 0.15;
    std::string error;
    ASSERT_TRUE(store.save(expected, error)) << error;

    const auto loaded = store.load(error);
    EXPECT_TRUE(error.empty()) << error;
    EXPECT_EQ(loaded.provider, "dml");
    EXPECT_EQ(loaded.cacheLimitBytes, 123456U);
    EXPECT_EQ(loaded.processingThreads, 12U);
    EXPECT_DOUBLE_EQ(loaded.sceneThreshold, 42.0);
    EXPECT_EQ(loaded.sceneMinFrames, 6U);
    EXPECT_DOUBLE_EQ(loaded.similarityThreshold, 0.91);
    EXPECT_DOUBLE_EQ(loaded.candidateThreshold, 0.61);
    EXPECT_DOUBLE_EQ(loaded.minRepeatGapSec, 7.0);
    EXPECT_DOUBLE_EQ(loaded.sameFileGapSec, 2.5);
    EXPECT_DOUBLE_EQ(loaded.crossFileGapSec, 1.25);
    EXPECT_DOUBLE_EQ(loaded.duplicateWindowSec, 1.75);
    EXPECT_DOUBLE_EQ(loaded.noiseFactor, 0.8);
    EXPECT_EQ(loaded.maxUniqueResults, 321U);
    EXPECT_DOUBLE_EQ(loaded.timeWeight, 0.35);
    EXPECT_DOUBLE_EQ(loaded.sakoeChibaRatio, 0.15);

    std::filesystem::remove_all(path.parent_path(), ignored);
}

} // namespace

TEST(SettingsStore, WriterSurvivesThrowingChangeAndCompletion) {
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    pfservices::SettingsStore store((directory.path() + "/settings.json").toStdString());
    std::atomic_bool reported = false;
    store.updateAsync("broken-change", [](auto&) { throw 42; },
        [&](std::string error) { reported = !error.empty(); throw std::runtime_error("callback"); });
    store.updateAsync("language", [](auto& settings) { settings.language = "ru"; });
    pfservices::SettingsStore::flushPendingWrites();
    EXPECT_TRUE(reported);
    std::string error;
    EXPECT_EQ(store.load(error).language, "ru");
    EXPECT_TRUE(error.empty());
}

TEST(SettingsStore, FailedReadCannotReplaceExistingSettingsWithDefaults) {
    QTemporaryDir directory;ASSERT_TRUE(directory.isValid());
    const auto path=directory.filePath("settings.json");
    const QByteArray original="{ interrupted settings json";
    {QFile file(path);ASSERT_TRUE(file.open(QIODevice::WriteOnly));ASSERT_EQ(file.write(original),original.size());}
    pfservices::SettingsStore store(path.toStdString());
    std::atomic_bool changed=false,reported=false;
    store.updateAsync("analysis",[&](auto& settings) {changed=true;settings.cachePath="replacement";},
        [&](std::string error) {reported=!error.empty();});
    pfservices::SettingsStore::flushPendingWrites();
    EXPECT_FALSE(changed);EXPECT_TRUE(reported);
    QFile file(path);ASSERT_TRUE(file.open(QIODevice::ReadOnly));EXPECT_EQ(file.readAll(),original);
}
