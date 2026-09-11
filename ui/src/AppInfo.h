#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>

namespace pfui {

// Thin read-only bridge exposed to QML as PfUiBridge.AppInfo (singleton).
// UI never computes anything here — GPU probing lives in pfgpu and the values
// are injected from app main's init step (spec section 2).
//
// The type is registered manually via registerQmlTypes() (called by the app and
// by UI tests before the engine loads). We deliberately do not use
// QML_ELEMENT + the module plugin: consumers would need to link and import the
// static plugin; manual registration keeps it explicit and testable.
class AppInfo : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QString gpuBackend READ gpuBackend NOTIFY gpuInfoChanged)
    Q_PROPERTY(QString gpuDevice READ gpuDevice NOTIFY gpuInfoChanged)
    Q_PROPERTY(QString gpuSummary READ gpuSummary NOTIFY gpuInfoChanged)
    Q_PROPERTY(QString ortVersion READ ortVersion NOTIFY gpuInfoChanged)
    Q_PROPERTY(bool backendIsGpu READ backendIsGpu NOTIFY gpuInfoChanged)

public:
    static AppInfo* instance();

    // Registers PfUiBridge.AppInfo as a QML singleton. Call once per process
    // before creating any QQmlEngine that uses it.
    static void registerQmlTypes();

    explicit AppInfo(QObject* parent = nullptr);

    QString version() const;

    QString gpuBackend() const;   // "auto" resolved: "cuda" / "dml" / "cpu" ...
    QString gpuDevice() const;    // "NVIDIA GeForce RTX 4070", "cpu (24 threads)"
    QString gpuSummary() const;   // display form: "cuda · NVIDIA GeForce RTX 4070"
    QString ortVersion() const;   // ONNX Runtime version, "" when not loaded
    bool backendIsGpu() const;

    // Single entry point for the init step: one signal for the whole badge
    // instead of four updates flickering through the UI.
    void setGpuInfo(const QString& backend,
                    const QString& device,
                    bool isGpu,
                    const QString& ortVersion);

signals:
    void gpuInfoChanged();

private:
    QString m_gpuBackend = QStringLiteral("cpu");
    QString m_gpuDevice;
    QString m_ortVersion;
    bool m_backendIsGpu = false;
};

} // namespace pfui
