#pragma once

#include <QObject>
#include <QVariantList>
#include <QStringList>

#include <memory>
#include <vector>

#include "pfcore/MotionMatcher.hpp"

namespace pfui {

class AnalysisController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int fileCount READ fileCount NOTIFY summaryChanged)
    Q_PROPERTY(qlonglong frameCount READ frameCount NOTIFY summaryChanged)
    Q_PROPERTY(double durationSeconds READ durationSeconds NOTIFY summaryChanged)
    Q_PROPERTY(int sceneCount READ sceneCount NOTIFY summaryChanged)
    Q_PROPERTY(int poseDetectionCount READ poseDetectionCount NOTIFY summaryChanged)
    Q_PROPERTY(int matchCount READ matchCount NOTIFY summaryChanged)
    Q_PROPERTY(QVariantList results READ results NOTIFY resultsChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString progressStage READ progressStage NOTIFY progressChanged)
    Q_PROPERTY(qlonglong processedFrames READ processedFrames NOTIFY progressChanged)
    Q_PROPERTY(qlonglong totalFrames READ totalFrames NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString providerChoice READ providerChoice WRITE setProviderChoice NOTIFY settingsChanged)
    Q_PROPERTY(QString qualityProfile READ qualityProfile WRITE setQualityProfile NOTIFY settingsChanged)
    Q_PROPERTY(bool normalizeSize READ normalizeSize WRITE setNormalizeSize NOTIFY settingsChanged)
    Q_PROPERTY(bool mirrorPoses READ mirrorPoses WRITE setMirrorPoses NOTIFY settingsChanged)
    Q_PROPERTY(QString modelPath READ modelPath NOTIFY settingsChanged)
    Q_PROPERTY(QString cachePath READ cachePath NOTIFY settingsChanged)
    Q_PROPERTY(double cacheLimitGb READ cacheLimitGb NOTIFY settingsChanged)
    Q_PROPERTY(double sceneThreshold READ sceneThreshold NOTIFY settingsChanged)
    Q_PROPERTY(double similarityThreshold READ similarityThreshold WRITE setSimilarityThreshold NOTIFY matcherParamsChanged)
    Q_PROPERTY(double candidateThreshold READ candidateThreshold WRITE setCandidateThreshold NOTIFY matcherParamsChanged)
    Q_PROPERTY(double repeatGap READ repeatGap WRITE setRepeatGap NOTIFY matcherParamsChanged)
    Q_PROPERTY(double sameFileGap READ sameFileGap WRITE setSameFileGap NOTIFY matcherParamsChanged)
    Q_PROPERTY(double crossFileGap READ crossFileGap WRITE setCrossFileGap NOTIFY matcherParamsChanged)
    Q_PROPERTY(double duplicateWindow READ duplicateWindow WRITE setDuplicateWindow NOTIFY matcherParamsChanged)
    Q_PROPERTY(double noiseFactor READ noiseFactor WRITE setNoiseFactor NOTIFY matcherParamsChanged)
    Q_PROPERTY(int maxUniqueResults READ maxUniqueResults WRITE setMaxUniqueResults NOTIFY matcherParamsChanged)
    Q_PROPERTY(double timeWeight READ timeWeight WRITE setTimeWeight NOTIFY matcherParamsChanged)
public:
    static AnalysisController* instance();
    static void registerQmlTypes();
    int fileCount() const noexcept { return fileCount_; }
    qlonglong frameCount() const noexcept { return frameCount_; }
    double durationSeconds() const noexcept { return durationSeconds_; }
    int sceneCount() const noexcept { return sceneCount_; }
    int poseDetectionCount() const noexcept { return poseDetectionCount_; }
    int matchCount() const noexcept { return matchCount_; }
    QVariantList results() const { return results_; }
    double progress() const noexcept { return progress_; }
    QString progressStage() const { return progressStage_; }
    qlonglong processedFrames() const noexcept { return processedFrames_; }
    qlonglong totalFrames() const noexcept { return totalFrames_; }
    QString status() const { return status_; }
    bool busy() const noexcept { return busy_; }
    QString providerChoice() const { return providerChoice_; }
    QString qualityProfile() const { return qualityProfile_; }
    bool normalizeSize() const noexcept { return normalizeSize_; }
    bool mirrorPoses() const noexcept { return mirrorPoses_; }
    QString modelPath() const { return modelPath_; }
    QString cachePath() const { return cachePath_; }
    double cacheLimitGb() const noexcept { return cacheLimitGb_; }
    double sceneThreshold() const noexcept { return sceneThreshold_; }
    double similarityThreshold() const noexcept { return similarityThreshold_; }
    double candidateThreshold() const noexcept { return candidateThreshold_; }
    void setSimilarityThreshold(double value);
    void setCandidateThreshold(double value);
    double repeatGap() const noexcept { return repeatGap_; }
    double sameFileGap() const noexcept { return sameFileGap_; }
    double crossFileGap() const noexcept { return crossFileGap_; }
    double duplicateWindow() const noexcept { return duplicateWindow_; }
    double noiseFactor() const noexcept { return noiseFactor_; }
    int maxUniqueResults() const noexcept { return maxUniqueResults_; }
    double timeWeight() const noexcept { return timeWeight_; }
    void setRepeatGap(double value);
    void setSameFileGap(double value);
    void setCrossFileGap(double value);
    void setDuplicateWindow(double value);
    void setNoiseFactor(double value);
    void setMaxUniqueResults(int value);
    void setTimeWeight(double value);
    void setProviderChoice(const QString& value);
    void setQualityProfile(const QString& value);
    void setNormalizeSize(bool value);
    void setMirrorPoses(bool value);

    Q_INVOKABLE void setModelPath(const QString& value);
    Q_INVOKABLE void setCachePath(const QString& value);
    Q_INVOKABLE void setCacheLimitGb(double value);
    Q_INVOKABLE void setSceneThreshold(double value);
    Q_INVOKABLE bool exportResults(const QString& format,
                                   int numberingMode,
                                   int cutMode,
                                   const QString& outputFolder,
                                   const QString& prefix,
                                   const QVariantList& selectedIndexes);

    Q_INVOKABLE void inspectFiles(const QStringList& paths);
    Q_INVOKABLE QStringList filesInFolder(const QString& folder) const;
    Q_INVOKABLE void analyzeFiles(const QStringList& paths);

signals:
    void summaryChanged();
    void resultsChanged();
    void progressChanged();
    void statusChanged();
    void busyChanged();
    void matcherParamsChanged();
    void settingsChanged();
    void exportFinished(bool success, const QString& message);

private:
    explicit AnalysisController(QObject* parent = nullptr);
    void setStatus(const QString& status);
    void saveMatcherSettings() const;
    void saveSettings() const;
    void setProgress(double value, const QString& stage, qlonglong processed, qlonglong total);
    int fileCount_ = 0;
    qlonglong frameCount_ = 0;
    double durationSeconds_ = 0.0;
    int sceneCount_ = 0;
    int poseDetectionCount_ = 0;
    int matchCount_ = 0;
    QVariantList results_;
    std::vector<pfcore::MotionMatch> matches_;
    double sourceFps_ = 0.0;
    QString status_;
    bool busy_ = false;
    double progress_ = 0.0;
    QString progressStage_;
    qlonglong processedFrames_ = 0;
    qlonglong totalFrames_ = 0;
    QString providerChoice_ = QStringLiteral("auto");
    QString qualityProfile_ = QStringLiteral("maximum");
    bool normalizeSize_ = true;
    bool mirrorPoses_ = true;
    QString modelPath_;
    QString cachePath_;
    double cacheLimitGb_ = 8.0;
    double sceneThreshold_ = 27.0;
    double similarityThreshold_ = 0.85;
    double candidateThreshold_ = 0.55;
    double repeatGap_ = 6.0;
    double sameFileGap_ = 2.0;
    double crossFileGap_ = 0.0;
    double duplicateWindow_ = 1.5;
    double noiseFactor_ = 1.0;
    int maxUniqueResults_ = 100;
    double timeWeight_ = 0.25;
};

} // namespace pfui
