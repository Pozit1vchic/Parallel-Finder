#include <QtPlugin>

// Static import of the PfUi module plugin (see ui/CMakeLists.txt). Shared-Qt
// builds never link the module plugin automatically, yet the generated qmldir
// references it, so consumers must import it explicitly.
Q_IMPORT_PLUGIN(PfUiPlugin)
