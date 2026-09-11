#pragma once

#include <QObject>
#include <QStringList>

namespace pfui {

class AnalysisController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int fileCount READ fileCount NOTIFY summaryChanged)
    Q_PROPERTY(qlonglong frameCount READ frameCount NOTIFY summaryChanged)
    Q_PROPERTY(double durationSeconds READ durationSeconds NOTIFY summaryChanged)
    Q_PROPERTY(int sceneCount READ sceneCount NOTIFY summaryChanged)
    Q_PROPERTY(int poseDetectionCount READ poseDetectionCount NOTIFY summaryChanged)
    Q_PROPERTY(int matchCount READ matchCount NOTIFY summaryChanged)
    Q_PROPERTY(QStringList resultItems READ resultItems NOTIFY summaryChanged)
    Q_PROPERTY(QStringList previewA READ previewA NOTIFY summaryChanged)
    Q_PROPERTY(QStringList previewB READ previewB NOTIFY summaryChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
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
    QStringList resultItems() const { return resultItems_; }
    QStringList previewA() const { return previewA_; }
    QStringList previewB() const { return previewB_; }
    QString status() const { return status_; }
    bool busy() const noexcept { return busy_; }
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

    Q_INVOKABLE void inspectFiles(const QStringList& paths);
    Q_INVOKABLE void analyzeFiles(const QStringList& paths);

signals:
    void summaryChanged();
    void statusChanged();
    void busyChanged();
    void matcherParamsChanged();

private:
    explicit AnalysisController(QObject* parent = nullptr);
    void setStatus(const QString& status);
    int fileCount_ = 0;
    qlonglong frameCount_ = 0;
    double durationSeconds_ = 0.0;
    int sceneCount_ = 0;
    int poseDetectionCount_ = 0;
    int matchCount_ = 0;
    QStringList resultItems_;
    QStringList previewA_;
    QStringList previewB_;
    QString status_;
    bool busy_ = false;
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
