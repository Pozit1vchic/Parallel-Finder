#pragma once

#include <QObject>
#include <QStringList>

namespace pfui {

class AnalysisController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(int fileCount READ fileCount NOTIFY summaryChanged)
    Q_PROPERTY(qlonglong frameCount READ frameCount NOTIFY summaryChanged)
    Q_PROPERTY(double durationSeconds READ durationSeconds NOTIFY summaryChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
public:
    static AnalysisController* instance();
    static void registerQmlTypes();
    int fileCount() const noexcept { return fileCount_; }
    qlonglong frameCount() const noexcept { return frameCount_; }
    double durationSeconds() const noexcept { return durationSeconds_; }
    QString status() const { return status_; }
    bool busy() const noexcept { return busy_; }

    Q_INVOKABLE void inspectFiles(const QStringList& paths);

signals:
    void summaryChanged();
    void statusChanged();
    void busyChanged();

private:
    explicit AnalysisController(QObject* parent = nullptr);
    void setStatus(const QString& status);
    int fileCount_ = 0;
    qlonglong frameCount_ = 0;
    double durationSeconds_ = 0.0;
    QString status_;
    bool busy_ = false;
};

} // namespace pfui
