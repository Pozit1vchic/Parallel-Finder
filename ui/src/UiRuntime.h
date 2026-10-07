#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QtGlobal>

namespace pfui {
inline void configureUiRuntime()
{
#ifdef Q_OS_WIN
    // Render independently of GUI work, using elapsed time for animations
    // rather than a driver-dependent VSync clock. Explicit overrides remain.
    if (!qEnvironmentVariableIsSet("QSG_RENDER_LOOP")) qputenv("QSG_RENDER_LOOP", "threaded");
    if (!qEnvironmentVariableIsSet("QSG_USE_SIMPLE_ANIMATION_DRIVER")) qputenv("QSG_USE_SIMPLE_ANIMATION_DRIVER", "1");

#endif
}
}
