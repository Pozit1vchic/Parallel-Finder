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

find_package(Qt6 6.8 REQUIRED COMPONENTS Quick QuickControls2 QuickDialogs2 Qml Gui Network)
qt_standard_project_setup()
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

# Ядро и экспортёры не зависят от Qt; UI-модуль статический (QML-модуль PfUi).
add_subdirectory(core)       # pfcore      — VideoDecoder, SceneDetector, DominantPerson, MotionMatcher, JobManager
add_subdirectory(gpu)        # pfgpu       — ORT-сессии, провайдеры auto/cuda/dml/cpu
add_subdirectory(services)   # pfservices  — ThumbnailCache, CutService, ThemeStore, settings
add_subdirectory(exporters)  # pfexporters — JSON/CSV/TXT/EDL/FCPXML/AEP
add_subdirectory(ui)         # pfui        — QML Main/Settings/Export + C++ bridge
add_subdirectory(app)        # ParallelFinder — thin: init → Main

if(BUILD_TESTING AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
endif()

# Additional labelled matcher regressions and benchmark checks.
if(BUILD_TESTING)
    add_subdirectory(tools/benchmark)
endif()
