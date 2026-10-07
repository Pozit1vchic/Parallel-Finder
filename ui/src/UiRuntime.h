#pragma once

#include <QtCore/QByteArray>
#include <QtCore/QtGlobal>

namespace pfui {
inline void configureUiRuntime()
{
#ifdef Q_OS_WIN
    // Keep rendering separate from GUI work, but let Qt synchronize animation
    // with the display. Its elapsed-time driver is an opt-in compatibility
    // mode and can introduce uneven motion on otherwise working VSync.
    if (!qEnvironmentVariableIsSet("QSG_RENDER_LOOP")) qputenv("QSG_RENDER_LOOP", "threaded");
    // The platform's default 5 ms coalescing delay consumes almost an entire
    // refresh interval on 180/240 Hz screens. Keep a small input-processing
    // allowance without disabling VSync or requesting frames while idle.
    if (!qEnvironmentVariableIsSet("QT_QPA_UPDATE_IDLE_TIME")) qputenv("QT_QPA_UPDATE_IDLE_TIME", "1");
#endif
}
}
