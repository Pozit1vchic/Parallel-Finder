// ParallelFinder — thin entry point (init → Main).
#include <QGuiApplication>
#include <QDebug>
#include <QEventLoop>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QTimer>
#include <QString>
#include <QUrl>
#include <QQuickWindow>

#include <pfgpu/DeviceInfo.hpp>
#include <pfservices/SettingsStore.hpp>
#include <pfservices/ProviderManager.hpp>
#include <QFileInfo>
#include <QFile>
#include <QDir>
#include <QSaveFile>
#include <QQmlContext>
#include <pfupdate/UpdateService.hpp>

#include <AppInfo.h>
#include <AnalysisController.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <QAbstractNativeEventFilter>
#include <QPointer>
#if defined(Q_OS_WIN)
#include <windows.h>
#endif

namespace {

#if defined(Q_OS_WIN)
class DesktopInstance final : public QAbstractNativeEventFilter {
public:
    DesktopInstance()
        : activationMessage_(RegisterWindowMessageW(L"ParallelFinder.ActivateDesktop"))
    {
        mutex_ = CreateMutexW(nullptr, FALSE, L"Local\\ParallelFinder.DesktopInstance");
        duplicate_ = mutex_ && GetLastError() == ERROR_ALREADY_EXISTS;
        if (duplicate_ && activationMessage_)
            PostMessageW(HWND_BROADCAST, activationMessage_, 0, 0);
    }
    ~DesktopInstance() override { if (mutex_) CloseHandle(mutex_); }
    bool valid() const { return mutex_ != nullptr; }
    bool duplicate() const { return duplicate_; }
    void setWindow(QQuickWindow* window) { window_ = window; }
    bool nativeEventFilter(const QByteArray&, void* message, qintptr*) override
    {
        const auto* event = static_cast<MSG*>(message);
        if (activationMessage_ && event->message == activationMessage_ && window_) {
            if (window_->visibility() == QWindow::Minimized) window_->showNormal();
            window_->show();
            window_->raise();
            window_->requestActivate();
        }
        return false;
    }
private:
    HANDLE mutex_ = nullptr;
    UINT activationMessage_ = 0;
    bool duplicate_ = false;
    QPointer<QQuickWindow> window_;
};
#endif

// Init step: one probe per process (memoized in pfgpu), results handed to the
// UI bridge. The app never computes backend decisions itself.
void publishGpuInfo()
{
    pfui::AppInfo::instance()->publishBackendProbe(pfgpu::probeBackends());
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
    QElapsedTimer startupTimer;
    startupTimer.start();
    bool providerProbe = false;
    bool diagnostic = false;
    for (int i = 1; i < argc; ++i) {
        providerProbe |= std::strcmp(argv[i], "--pf-provider-probe") == 0;
        diagnostic |= std::strcmp(argv[i], "--pf-smoke") == 0
            || std::strcmp(argv[i], "--pf-analysis-smoke") == 0
            || std::strcmp(argv[i], "--pf-ui-smoke") == 0;
        diagnostic |= std::strcmp(argv[i], "--pf-update-healthcheck") == 0;
    }
#if defined(Q_OS_WIN)
    if (providerProbe)
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    // Gate desktop launches before initializing Qt GUI or any GPU runtime.
    // Isolated provider/diagnostic workers are deliberately not desktop instances.
    std::unique_ptr<DesktopInstance> desktopInstance;
    if (!providerProbe && !diagnostic) {
        desktopInstance = std::make_unique<DesktopInstance>();
        if (!desktopInstance->valid()) {
            std::fprintf(stderr, "Cannot acquire ParallelFinder desktop instance lock\n");
            return 1;
        }
        if (desktopInstance->duplicate()) return 0;
    }
#endif
    // A provider probe has no window or QML. QGuiApplication would try to load
    // an offscreen platform plugin absent from the shipped Windows package.
    std::unique_ptr<QCoreApplication> application;
    if (providerProbe) application = std::make_unique<QCoreApplication>(argc, argv);
    else application = std::make_unique<QGuiApplication>(argc, argv);
    auto& app = *application;
    if (qEnvironmentVariableIsSet("PF_DEBUG_STARTUP"))
        std::fprintf(stderr, "PF_STARTUP application_ms=%lld\n", static_cast<long long>(startupTimer.elapsed()));
#if defined(Q_OS_WIN)
    if (desktopInstance) app.installNativeEventFilter(desktopInstance.get());
#endif
    // QStandardPaths uses these identifiers for %LocalAppData%/ParallelFinder.
    // Set them before the UI singleton constructs SettingsStore or ModelStore.
    QCoreApplication::setOrganizationName(QStringLiteral("ParallelFinder"));
    QCoreApplication::setApplicationName(QStringLiteral("ParallelFinder"));
    QGuiApplication::setApplicationVersion(QStringLiteral(PF_VERSION));

    // Downloaded runtimes live in writable per-user storage, not Program Files.
    if (!qEnvironmentVariableIsSet("PF_PROVIDER_ROOT") && !qEnvironmentVariableIsSet("PF_ORT_DLL")) {
        const auto root = QString::fromStdString(pfservices::SettingsStore::defaultDirectory()) + QStringLiteral("/providers");
        std::string settingsError;
        const auto chosen = pfservices::SettingsStore().load(settingsError).provider;
        const auto appRoot = QCoreApplication::applicationDirPath() + QStringLiteral("/providers/");
        const auto name = QString::fromStdString(chosen);
        const auto activate = [&](const QString& provider) {
            const auto runtime = pfservices::ProviderManager::locateRuntime({root, appRoot}, provider);
            if (!runtime.isEmpty()) {
                qputenv("PF_PROVIDER_ROOT", QFileInfo(runtime).absolutePath().toUtf8());
                qputenv("PF_ORT_DLL", runtime.toUtf8());
            }
        };
        if (chosen == "cuda" || chosen == "tensorrt" || chosen == "dml") {
            activate(name);
        }
        QFile marker(root + QStringLiteral("/active.txt"));
        if (!qEnvironmentVariableIsSet("PF_PROVIDER_ROOT") && marker.open(QIODevice::ReadOnly)) {
            const auto provider = QString::fromUtf8(marker.read(32)).trimmed();
            if (provider == "dml" || provider == "cuda" || provider == "tensorrt")
                activate(provider);
        }
    }

#if defined(Q_OS_WIN)
    // CUDA/cuDNN/TensorRT load several secondary DLLs lazily, after
    // onnxruntime.dll itself is already loaded. Keep the selected provider
    // directory first in this process' PATH so those transitive DLLs resolve
    // without modifying the user's/system PATH.
    if (qEnvironmentVariableIsSet("PF_PROVIDER_ROOT")) {
        const QByteArray providerRoot = qgetenv("PF_PROVIDER_ROOT");
        const QByteArray oldPath = qgetenv("PATH");
        if (!providerRoot.isEmpty()) {
            QByteArray newPath = providerRoot;
            if (!oldPath.isEmpty()) {
                newPath += ';';
                newPath += oldPath;
            }
            qputenv("PATH", newPath);
        }
    }
#endif

    // TensorRT's first session build is expensive. Persist both engine and
    // timing caches in the normal writable application cache so subsequent
    // runs (including after an app restart) can deserialize instead of
    // rebuilding the engine. ProviderFactory consumes this path via the ORT
    // TensorRT V2 provider options.
    if (!qEnvironmentVariableIsSet("PF_TRT_CACHE_PATH")) {
        std::string cacheSettingsError;
        const auto cacheSettings = pfservices::SettingsStore().load(cacheSettingsError);
        const QString cacheRoot = cacheSettings.cachePath.empty()
            ? QString::fromStdString(pfservices::SettingsStore::defaultDirectory()) + QStringLiteral("/cache")
            : QString::fromStdString(cacheSettings.cachePath);
        const QString trtCache = QDir(cacheRoot).filePath(QStringLiteral("tensorrt"));
        QDir().mkpath(trtCache);
        qputenv("PF_TRT_CACHE_PATH", QDir::toNativeSeparators(trtCache).toUtf8());
    }

    // Each child loads exactly one bundle. Never load another ORT DLL into a
    // GUI process with live sessions just to discover an installed provider.
    const auto probeIndex = app.arguments().indexOf(QStringLiteral("--pf-provider-probe"));
    if (probeIndex >= 0) {
        const auto name = app.arguments().value(probeIndex + 1);
        const auto provider = pfgpu::parseProvider(name.toStdString());
        if (!provider || *provider == pfgpu::Provider::Auto) return 2;
        const auto* status = pfgpu::findBackendStatus(*provider);
        const QJsonObject object{{"provider", name}, {"available", status && status->available},
            {"reason", status ? QString::fromStdString(status->reason) : QStringLiteral("Unknown provider")}};
        const auto json = QJsonDocument(object).toJson(QJsonDocument::Compact);
        std::printf("PF_PROVIDER_JSON=%s\n", json.constData());
        return 0;
    }

    const bool windowDiagnostic = app.arguments().contains(QStringLiteral("--pf-ui-smoke"))
        || app.arguments().contains(QStringLiteral("--pf-update-healthcheck"));
    if (diagnostic && !windowDiagnostic) publishGpuInfo();
    else {
        auto* info = pfui::AppInfo::instance();
        if (qEnvironmentVariableIsSet("PF_DEBUG_STARTUP"))
            QObject::connect(info, &pfui::AppInfo::backendInitializationChanged, &app, [info, startupTimer] {
                if (!info->backendInitializing())
                    std::fprintf(stderr, "PF_STARTUP backend_ready_ms=%lld\n", static_cast<long long>(startupTimer.elapsed()));
            });
        // CUDA/DirectML initialization can contend with the Qt renderer in
        // the driver even on another thread. Publish pending now, but do not
        // start loading providers until the first window frame is presented.
        info->prepareBackendInitialization();
    }
    pfui::AppInfo::registerQmlTypes();
    if (qEnvironmentVariableIsSet("PF_DEBUG_STARTUP"))
        std::fprintf(stderr, "PF_STARTUP bridge_ms=%lld\n", static_cast<long long>(startupTimer.elapsed()));

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
    engine.addImportPath(QCoreApplication::applicationDirPath() + QStringLiteral("/qml"));
    // Load by URL: loadFromModule() needs the module's plugin registered,
    // which shared-Qt builds of static modules do not do automatically.
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/PfUi/qml/Main.qml")));
    if (qEnvironmentVariableIsSet("PF_DEBUG_STARTUP"))
        std::fprintf(stderr, "PF_STARTUP qml_ms=%lld\n", static_cast<long long>(startupTimer.elapsed()));
    if (engine.rootObjects().isEmpty()) {
        std::fprintf(stderr, "Fatal: failed to load PfUi.Main\n");
        return 1;
    }
#if defined(Q_OS_WIN)
    if (desktopInstance)
        desktopInstance->setWindow(qobject_cast<QQuickWindow*>(engine.rootObjects().first()));
#endif
    if (auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first())) {
        QObject::connect(window, &QQuickWindow::frameSwapped, &app, [] {
            pfui::AppInfo::instance()->initializeBackendsAsync();
        }, Qt::SingleShotConnection);
    }
    if (qEnvironmentVariableIsSet("PF_DEBUG_STARTUP")) {
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if (window) QObject::connect(window, &QQuickWindow::frameSwapped, &app, [startupTimer] {
            std::fprintf(stderr, "PF_STARTUP first_frame_ms=%lld backend_pending=%d\n",
                static_cast<long long>(startupTimer.elapsed()), pfui::AppInfo::instance()->backendInitializing() ? 1 : 0);
        }, Qt::SingleShotConnection);
    }
    // Unlike --pf-smoke, exercise the shipped QML imports and actual renderer.
    if (args.contains(QStringLiteral("--pf-ui-smoke")) || args.contains(QStringLiteral("--pf-update-healthcheck"))) {
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        if (!window) return 2;
        QObject::connect(window, &QQuickWindow::frameSwapped, &app, [&app] {
            std::printf("ParallelFinder UI smoke: window rendered\n");
            if (qEnvironmentVariableIsSet("PF_UI_SMOKE_WAIT_BACKEND")) {
                auto* info = pfui::AppInfo::instance();
                if (info->backendInitializing()) {
                    QObject::connect(info, &pfui::AppInfo::backendInitializationChanged, &app, [&app, info] {
                        if (!info->backendInitializing()) app.exit(0);
                    });
                    return;
                }
            }
            app.exit(0);
        }, Qt::SingleShotConnection);
        QTimer::singleShot(15000, &app, [&app] { app.exit(3); });
        QTimer::singleShot(100, window, [window] {
            // A hidden launcher can suppress the first native ShowWindow call.
            // The render probe must expose the window, not merely load QML.
            window->hide();
            window->show();
            window->requestUpdate();
        });
    }
    if (!diagnostic) {
        const auto type=qmlTypeId("PfUiBridge",1,0,"Updates");
        auto* updates=engine.singletonInstance<pfupdate::UpdateService*>(type);
        QObject::connect(updates,&pfupdate::UpdateService::quitRequested,&app,&QCoreApplication::quit);
        const auto ackIndex=args.indexOf("--pf-update-ack");
        if(ackIndex>=0 && ackIndex+1<args.size()) {
            const auto ack=args[ackIndex+1];
            if(auto* window=qobject_cast<QQuickWindow*>(engine.rootObjects().first()))
                QObject::connect(window,&QQuickWindow::frameSwapped,&app,[ack]{
                    QSaveFile file(ack);if(file.open(QIODevice::WriteOnly)){file.write(QCoreApplication::applicationVersion().toUtf8());file.commit();}
                },Qt::SingleShotConnection);
        }
        updates->startup();
    }
    return app.exec();
}
