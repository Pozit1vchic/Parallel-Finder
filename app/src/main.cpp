// ParallelFinder — thin entry point (spec section 2: init → Splash → Main).
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QUrl>

#include <pfgpu/DeviceInfo.hpp>

#include <AppInfo.h>
#include <cstdio>

int main(int argc, char* argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationVersion(QStringLiteral(PF_VERSION));

    // Init step: probe backend (stage 1 replaces the stub) and feed the badge.
    pfui::AppInfo::instance()->setGpuBackend(
        QString::fromLatin1(pfgpu::providerName(pfgpu::defaultProvider())));
    pfui::AppInfo::registerQmlTypes();

    // CI/dev smoke path: verify the binary runs, no window needed.
    const QStringList args = app.arguments();
    if (args.contains(QStringLiteral("--pf-smoke"))) {
        std::printf("ParallelFinder v%s smoke ok (backend=%s, ort=%s)\n",
                    qPrintable(QGuiApplication::applicationVersion()),
                    qPrintable(pfui::AppInfo::instance()->gpuBackend()),
                    pfgpu::ortRuntimeAvailable() ? "yes" : "no");
        return 0;
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
