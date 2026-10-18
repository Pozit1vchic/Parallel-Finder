find_package(GTest CONFIG REQUIRED)
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
set_target_properties(pf_pose_batch_probe PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_link_libraries(pf_pose_batch_probe PRIVATE pf::gpu Qt6::Core Qt6::Gui)

# Benchmark regression is part of the test suite; the remaining executables
# are developer diagnostics and never belong in the application directory.
pf_set_executable_output_group(pf_matcher_regression tests)
foreach(target IN ITEMS
        pf_decode_probe
        pf_matcher_probe
        pf_face_probe
        pf_dominant_probe
        pf_scene_memory_probe
        pf_view_probe
        pf_grouping_probe
        pf_cut_probe
        pf_scene_candidates_probe
        pf_cuda_graph_probe
        pf_pose_batch_probe)
    pf_set_executable_output_group(${target} tools)
endforeach()
