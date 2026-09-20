// ParallelFinder — thin entry point (init → Main).
#include <QGuiApplication>
#include <QDebug>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QQmlApplicationEngine>
#include <QTimer>
#include <QString>
#include <QUrl>

#include <pfgpu/DeviceInfo.hpp>

#include <AppInfo.h>
#include <AnalysisController.h>
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
    // QStandardPaths uses these identifiers for %LocalAppData%/ParallelFinder.
    // Set them before the UI singleton constructs SettingsStore or ModelStore.
    QCoreApplication::setOrganizationName(QStringLiteral("ParallelFinder"));
    QCoreApplication::setApplicationName(QStringLiteral("ParallelFinder"));
    QGuiApplication::setApplicationVersion(QStringLiteral(PF_VERSION));

    publishGpuInfo();
    pfui::AppInfo::registerQmlTypes();

    const QStringList args = app.arguments();
    if (args.contains(QStringLiteral("--pf-smoke"))) {
        return runSmoke();
    }
    const int analysisSmokeIndex = args.indexOf(QStringLiteral("--pf-analysis-smoke"));
    if (analysisSmokeIndex >= 0) {
        const QStringList paths = args.mid(analysisSmokeIndex + 1);
        if (paths.isEmpty()) {
            std::fprintf(stderr, "--pf-analysis-smoke requires at least one video path\n");
            return 2;
        }
        auto* analysis = pfui::AnalysisController::instance();
        QEventLoop loop;
        QTimer timeout;
        timeout.setSingleShot(true);
        const int requestedTimeout = qEnvironmentVariableIntValue("PF_ANALYSIS_TIMEOUT_SEC");
        timeout.setInterval((requestedTimeout > 0 ? std::min(requestedTimeout, 3600) : 120) * 1000);
        QElapsedTimer elapsed;
        elapsed.start();
        QObject::connect(analysis, &pfui::AnalysisController::busyChanged, &loop, [&] {
            if (!analysis->busy()) loop.quit();
        });
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        timeout.start();
        analysis->analyzeFiles(paths);
        // Validation failures (missing model/provider or invalid input) are
        // reported synchronously and deliberately never toggle `busy`.  Do
        // not make headless diagnostics wait for the full timeout in that
        // case; the UI path already displays the status immediately.
        if (!analysis->busy())
            QTimer::singleShot(0, &loop, &QEventLoop::quit);
        loop.exec();
        std::printf("ParallelFinder analysis smoke %s\n", analysis->busy() ? "timeout" : "ok");
        std::printf("  status : %s\n", qPrintable(analysis->status()));
        std::printf("  files  : %d\n", analysis->fileCount());
        std::printf("  frames : %lld\n", static_cast<long long>(analysis->frameCount()));
        std::printf("  pairs  : %d\n", analysis->matchCount());
        std::printf("  elapsed_ms : %lld\n", static_cast<long long>(elapsed.elapsed()));
        if (qEnvironmentVariableIsSet("PF_ANALYSIS_JSON")) {
            const auto json = QJsonDocument::fromVariant(analysis->results()).toJson(QJsonDocument::Compact);
            std::printf("  results_json : %s\n", json.constData());
        }
        const int minimumPairs = qEnvironmentVariableIntValue("PF_ANALYSIS_MIN_PAIRS");
        return analysis->busy() || !analysis->analysisCompleted()
            || analysis->matchCount() < minimumPairs ? 1 : 0;
    }

    QQmlApplicationEngine engine;
    QObject::connect(&engine, &QQmlApplicationEngine::warnings, [](const QList<QQmlError>& warnings) {
        for (const QQmlError& warning : warnings) {
            std::fprintf(stderr, "QML: %s\n", qPrintable(warning.toString()));
        }
    });
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
