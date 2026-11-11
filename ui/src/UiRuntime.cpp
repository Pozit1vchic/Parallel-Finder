#include "UiRuntime.h"
#ifdef Q_OS_WIN
#include <roapi.h>
#endif

namespace pfui {
GuiApartment::GuiApartment(bool enabled) noexcept {
#ifdef Q_OS_WIN
    if(enabled)ownsReference_=SUCCEEDED(RoInitialize(RO_INIT_SINGLETHREADED));
#else
    (void)enabled;
#endif
}
GuiApartment::~GuiApartment() {
#ifdef Q_OS_WIN
    // S_FALSE also adds a reference. Failed initialization (including an
    // existing incompatible apartment) must never be uninitialized by us.
    if(ownsReference_)RoUninitialize();
#endif
}
}
