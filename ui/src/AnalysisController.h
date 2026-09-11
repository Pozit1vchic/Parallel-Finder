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
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(double similarityThreshold READ similarityThreshold WRITE setSimilarityThreshold NOTIFY matcherParamsChanged)
    Q_PROPERTY(double candidateThreshold READ candidateThreshold WRITE setCandidateThreshold NOTIFY matcherParamsChanged)
public:
    static AnalysisController* instance();
    static void registerQmlTypes();
    int fileCount() const noexcept { return fileCount_; }
    qlonglong frameCount() const noexcept { return frameCount_; }
    double durationSeconds() const noexcept { return durationSeconds_; }
    int sceneCount() const noexcept { return sceneCount_; }
    int poseDetectionCount() const noexcept { return poseDetectionCount_; }
    int matchCount() const noexcept { return matchCount_; }
    QString status() const { return status_; }
    bool busy() const noexcept { return busy_; }
    double similarityThreshold() const noexcept { return similarityThreshold_; }
    double candidateThreshold() const noexcept { return candidateThreshold_; }
    void setSimilarityThreshold(double value);
    void setCandidateThreshold(double value);

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
    QString status_;
    bool busy_ = false;
    double similarityThreshold_ = 0.85;
    double candidateThreshold_ = 0.55;
};

} // namespace pfui
