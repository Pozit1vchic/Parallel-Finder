qt_add_executable(ParallelFinder
    src/main.cpp
    ParallelFinder.rc
)

target_compile_definitions(ParallelFinder PRIVATE PF_VERSION="${PROJECT_VERSION}")
target_link_libraries(ParallelFinder PRIVATE
    pfui               # QML module PfUi (static): Theme/L10n/Splash/Main + AppInfo
    pfui_module_import # static import of the PfUi module plugin
    pfgpu              # init step reads the active backend for the GPU badge
)

set_target_properties(ParallelFinder PROPERTIES
    MACOSX_BUNDLE FALSE
    WIN32_EXECUTABLE TRUE
)
