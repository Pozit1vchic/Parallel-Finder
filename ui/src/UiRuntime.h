#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QtGlobal>

namespace pfui {
inline void configureUiRuntime()
{
#ifdef Q_OS_WIN
    // Windows driver/VSync synchronization can stop the threaded animation
    // clock during window/effect changes. Keep GPU rendering, but let Qt's
    // basic loop drive finite animations from system timers. An explicit
    // diagnostic/user override remains available.
    if (!qEnvironmentVariableIsSet("QSG_RENDER_LOOP")) qputenv("QSG_RENDER_LOOP", "basic");
    // Avoid Qt's extra update-idle delay on top of the animation timer and
    // VSync wait. Updates remain event-driven; idle windows do not repaint.
    if (!qEnvironmentVariableIsSet("QT_QPA_UPDATE_IDLE_TIME")) qputenv("QT_QPA_UPDATE_IDLE_TIME", "0");
#endif
}
}
