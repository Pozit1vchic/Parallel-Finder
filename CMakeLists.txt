cmake_minimum_required(VERSION 3.25)

project(ParallelFinder
    VERSION 0.1.0
    DESCRIPTION "Parallel-Finder: поиск параллельных движений в видео"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic)
elseif(MSVC)
    add_compile_options(/W4 /permissive-)
endif()

include(CTest)

find_package(Qt6 6.5 REQUIRED COMPONENTS Quick QuickControls2 Qml Gui)
qt_standard_project_setup()
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

# Ядро и экспортёры не зависят от Qt; UI-модуль статический (QML-модуль PfUi).
add_subdirectory(core)       # pfcore      — VideoDecoder, SceneDetector, PoseEstimator, MotionMatcher, JobManager
add_subdirectory(gpu)        # pfgpu       — ORT-сессии, провайдеры auto/cuda/dml/cpu
add_subdirectory(services)   # pfservices  — ThumbnailCache, CutService, ThemeStore, settings
add_subdirectory(exporters)  # pfexporters — JSON/CSV/TXT/EDL/FCPXML/AEP
add_subdirectory(ui)         # pfui        — QML Splash/Main/Settings/DemoMode + C++ bridge
add_subdirectory(app)        # ParallelFinder — thin: init → Splash → Main

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
