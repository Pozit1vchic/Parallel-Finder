find_package(GTest CONFIG REQUIRED)
add_executable(pf_classifier_interval_bench classifier_interval_bench.cpp)
pf_set_executable_output_group(pf_classifier_interval_bench tools)
set_target_properties(pf_classifier_interval_bench PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_link_libraries(pf_classifier_interval_bench PRIVATE pf::core)
find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME pf_quality_metrics COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_quality_metrics.py")
endif()
add_executable(pf_matcher_regression
    test_motion_matcher.cpp
    test_scene_detector.cpp
)
set_target_properties(pf_matcher_regression PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_link_libraries(pf_matcher_regression PRIVATE pf::core GTest::gtest_main)
add_test(NAME pf_matcher_regression COMMAND pf_matcher_regression)

add_executable(pf_decode_probe decode_probe.cpp)
set_target_properties(pf_decode_probe PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_link_libraries(pf_decode_probe PRIVATE pf::core)

add_executable(pf_matcher_probe matcher_probe.cpp)
target_compile_definitions(pf_matcher_probe PRIVATE PF_ORGANIZATION_PROBE)
add_executable(pf_character_probe character_probe.cpp)
add_executable(pf_posture_probe posture_probe.cpp)
add_executable(pf_provider_schedule_probe provider_schedule_probe.cpp)
add_executable(pf_body_orientation_probe body_orientation_probe.cpp)
set_target_properties(pf_body_orientation_probe PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_link_libraries(pf_body_orientation_probe PRIVATE pf::gpu Qt6::Core Qt6::Gui)
set_target_properties(pf_provider_schedule_probe PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(pf_provider_schedule_probe PRIVATE "${PROJECT_SOURCE_DIR}/services/src" "${PROJECT_SOURCE_DIR}/services/include")
target_link_libraries(pf_provider_schedule_probe PRIVATE Qt6::Core)
set_target_properties(pf_posture_probe PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_link_libraries(pf_posture_probe PRIVATE pf::core Qt6::Core)
add_test(NAME pf_real_posture_controls COMMAND pf_posture_probe
    "${PROJECT_SOURCE_DIR}/tests/fixtures/butcher-posture-observations.json" --verify)
set_target_properties(pf_character_probe PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_link_libraries(pf_character_probe PRIVATE pf::core Qt6::Core)
set_target_properties(pf_matcher_probe PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_link_libraries(pf_matcher_probe PRIVATE pf::core Qt6::Core)

add_executable(pf_face_probe face_probe.cpp)
set_target_properties(pf_face_probe PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_link_libraries(pf_face_probe PRIVATE pf::gpu Qt6::Core Qt6::Gui)

# Explicit scaling controls; these are benchmarks, never semantic ground truth.
add_executable(pf_dominant_probe dominant_probe.cpp)
set_target_properties(pf_dominant_probe PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_link_libraries(pf_dominant_probe PRIVATE pf::core)
add_executable(pf_scene_memory_probe scene_memory_probe.cpp)
set_target_properties(pf_scene_memory_probe PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_link_libraries(pf_scene_memory_probe PRIVATE pf::core)
if(WIN32)
    target_link_libraries(pf_scene_memory_probe PRIVATE psapi)
endif()

add_executable(pf_view_probe view_probe.cpp)
set_target_properties(pf_view_probe PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_link_libraries(pf_view_probe PRIVATE pf::core)
add_executable(pf_grouping_probe grouping_probe.cpp)
set_target_properties(pf_grouping_probe PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_link_libraries(pf_grouping_probe PRIVATE pf::core pf::services Qt6::Core)
add_executable(pf_cut_probe cut_probe.cpp)
set_target_properties(pf_cut_probe PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_link_libraries(pf_cut_probe PRIVATE pf::core Qt6::Core)
add_executable(pf_scene_candidates_probe scene_candidates_probe.cpp)
set_target_properties(pf_scene_candidates_probe PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_link_libraries(pf_scene_candidates_probe PRIVATE pf::core pf::services Qt6::Core)

# Standalone experiment: never changes application sessions or user caches.
add_executable(pf_cuda_graph_probe cuda_graph_probe.cpp)
set_target_properties(pf_cuda_graph_probe PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_link_libraries(pf_cuda_graph_probe PRIVATE pf::gpu Qt6::Core Qt6::Gui)
add_executable(pf_pose_batch_probe pose_batch_probe.cpp)
add_executable(pf_pose3d_probe pose3d_probe.cpp)
set_target_properties(pf_pose3d_probe PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_link_libraries(pf_pose3d_probe PRIVATE pf::gpu pf::core Qt6::Core Qt6::Gui)
set_target_properties(pf_pose_batch_probe PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_link_libraries(pf_pose_batch_probe PRIVATE pf::gpu Qt6::Core Qt6::Gui)

# Benchmark regression is part of the test suite; the remaining executables
# are developer diagnostics and never belong in the application directory.
pf_set_executable_output_group(pf_matcher_regression tests)
foreach(target IN ITEMS
        pf_decode_probe
        pf_matcher_probe
        pf_character_probe
        pf_posture_probe
        pf_provider_schedule_probe
        pf_body_orientation_probe
        pf_face_probe
        pf_dominant_probe
        pf_scene_memory_probe
        pf_view_probe
        pf_grouping_probe
        pf_cut_probe
        pf_scene_candidates_probe
        pf_cuda_graph_probe
        pf_pose_batch_probe
        pf_pose3d_probe)
    pf_set_executable_output_group(${target} tools)
endforeach()
