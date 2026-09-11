// ParallelFinder — thin entry point (spec section 2: init → Splash → Main).
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QString>
#include <QUrl>

#include <pfgpu/DeviceInfo.hpp>

#include <AppInfo.h>
#include <cstdio>

namespace {

// Init step: one probe per process (memoized in pfgpu), results handed to the
// UI bridge. The app never computes backend decisions itself.
void publishGpuInfo()
{
    const pfgpu::BackendProbe& probe = pfgpu::probeBackends();
    const pfgpu::Provider provider = pfgpu::defaultProvider();
    const pfgpu::BackendStatus* status = pfgpu::findBackendStatus(provider);

    pfui::AppInfo::instance()->setGpuInfo(
        QString::fromLatin1(pfgpu::providerName(provider)),
        status ? QString::fromStdString(status->deviceName) : QString(),
        provider != pfgpu::Provider::Cpu,
        QString::fromStdString(probe.ortVersion));
}

// --pf-smoke: CI/dev path that proves the binary runs without a window and
// dumps the fallback chain (the same text the badge tooltip shows).
int runSmoke()
{
    const pfgpu::BackendProbe& probe = pfgpu::probeBackends();
    const pfgpu::Provider provider = pfgpu::defaultProvider();
    const pfgpu::BackendStatus* status = pfgpu::findBackendStatus(provider);

    std::printf("ParallelFinder v%s smoke ok\n",
                qPrintable(QGuiApplication::applicationVersion()));
    std::printf("  backend    : %s", pfgpu::providerName(provider));
    if (status && !status->deviceName.empty()) {
        std::printf(" (%s)", status->deviceName.c_str());
    }
    std::printf("\n");
    std::printf("  onnxruntime: %s\n",
                probe.ortLoaded ? probe.ortVersion.c_str() : "not loaded");
    if (!probe.ortLoaded) {
        std::printf("  runtime err: %s\n", probe.ortError.c_str());
    } else {
        std::printf("  ort api    : v%u\n", probe.ortApiVersion);
    }
    for (const pfgpu::BackendStatus& candidate : probe.backends) {
        std::printf("  %-9s: %s", pfgpu::providerName(candidate.provider),
                    candidate.available ? "available" : "unavailable");
        if (!candidate.available && !candidate.reason.empty()) {
            std::printf(" — %s", candidate.reason.c_str());
        }
        std::printf("\n");
    }
    return 0;
}

} // namespace

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationVersion(QStringLiteral(PF_VERSION));

    publishGpuInfo();
    pfui::AppInfo::registerQmlTypes();

    const QStringList args = app.arguments();
    if (args.contains(QStringLiteral("--pf-smoke"))) {
        return runSmoke();
    }

    QQmlApplicationEngine engine;
    // Static QML module resources live under :/qt/qml (QTP0001); make the
    // import path explicit so `import PfUi` resolves regardless of Qt defaults.
    engine.addImportPath(QStringLiteral("qrc:/qt/qml"));
    // Load by URL: loadFromModule() needs the module's plugin registered,
    // which shared-Qt builds of static modules do not do automatically.
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/PfUi/qml/Main.qml")));
    if (engine.rootObjects().isEmpty()) {
        std::fprintf(stderr, "Fatal: failed to load PfUi.Main\n");
        return 1;
    }
    return app.exec();
}
