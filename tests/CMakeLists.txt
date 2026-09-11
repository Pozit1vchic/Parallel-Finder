enable_testing()

find_package(GTest CONFIG REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Test)

# ── Core tests (Google Test): pfcore + pfservices + pfgpu ──────────────────
add_executable(pf_tests
    core/test_scene_detector.cpp
    core/test_video_decoder.cpp
    core/test_motion_matcher.cpp
    core/test_thumbnail_cache.cpp
    gpu/test_device_info.cpp
    gpu/test_provider.cpp
    gpu/test_provider_factory.cpp
    gpu/test_session_cache.cpp
)
set_target_properties(pf_tests PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_compile_definitions(pf_tests PRIVATE PF_TEST_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
target_link_libraries(pf_tests PRIVATE
    pf::core
    pf::services
    pf::gpu
    GTest::gtest_main
)
add_test(NAME pf_tests COMMAND pf_tests)

# ── UI smoke (QTest): static QML module PfUi loads, singletons resolve ─────
add_executable(pfui_tests
    ui/test_ui_smoke.cpp
)
target_link_libraries(pfui_tests PRIVATE
    pfui               # links the static QML module incl. resources
    pfui_module_import # static import of the PfUi module plugin
    Qt6::Test
)
add_test(NAME pfui_tests COMMAND pfui_tests)
set_tests_properties(pfui_tests PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

# ── App smoke: binary runs and reports the init path ───────────────────────
add_test(NAME app_smoke COMMAND ParallelFinder --pf-smoke)
set_tests_properties(app_smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
