qt_add_library(pfui STATIC)

target_include_directories(pfui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_features(pfui PUBLIC cxx_std_23)

set(PFUI_QML_FILES
    qml/Theme.qml
    qml/L10n.qml
    qml/Splash.qml
    qml/Main.qml
    qml/PfSlider.qml
    qml/PfCheckBox.qml
    qml/PfButton.qml
    qml/PfIconButton.qml
    qml/TopBar.qml
    qml/StatsStrip.qml
    qml/SourcesRail.qml
    qml/MotionCenter.qml
    qml/ResultsRail.qml
    qml/SettingsDialog.qml
    qml/ExportDialog.qml
)
set_source_files_properties(qml/Theme.qml qml/L10n.qml
    PROPERTIES QT_QML_SINGLETON_TYPE TRUE
)

qt_add_qml_module(pfui
    URI PfUi
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/PfUi
    QML_FILES ${PFUI_QML_FILES}
    RESOURCES
        qml/pf_logo.png
        qml/assets/film.svg
        qml/assets/plus.svg
        qml/assets/folder-plus.svg
        qml/assets/settings.svg
        qml/assets/chevron-left.svg
        qml/assets/chevron-right.svg
        qml/assets/x.svg
        qml/assets/play.svg
        qml/assets/download.svg
        qml/assets/check.svg
        qml/assets/minus.svg
        qml/assets/maximize.svg
)

# C++ bridge is part of the pfui library but NOT part of the QML module:
# a pure-QML module generates no plugin, and AppInfo is registered manually
# (AppInfo::registerQmlTypes) by the app/tests. See AppInfo.h.
target_sources(pfui PRIVATE
    src/AppInfo.cpp
    src/AnalysisController.cpp
)
target_link_libraries(pfui PUBLIC pf::core pf::gpu pf::services pf::exporters)

# Static import of the module plugin: qt_add_qml_module writes "optional
# plugin pfuiplugin" into qmldir, but shared-Qt builds do not link static
# module plugins automatically — the import fails at runtime. An OBJECT
# library guarantees the Q_IMPORT_PLUGIN initializer reaches every consumer's
# link line (static-init archive members would never be pulled on demand).
# Consumers link both pfui and pfui_module_import.
add_library(pfui_module_import OBJECT src/PfUiModuleImport.cpp)
target_link_libraries(pfui_module_import PUBLIC Qt6::Core pfuiplugin)

target_link_libraries(pfui PUBLIC
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::QuickDialogs2
)

target_compile_definitions(pfui PRIVATE PF_VERSION="${PROJECT_VERSION}")
