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

add_executable(pf_scene_probe scene_probe.cpp)
set_target_properties(pf_scene_probe PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_link_libraries(pf_scene_probe PRIVATE pf::core Qt6::Core)

add_executable(pf_matcher_probe matcher_probe.cpp)
set_target_properties(pf_matcher_probe PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_link_libraries(pf_matcher_probe PRIVATE pf::core Qt6::Core)

add_executable(pf_face_probe face_probe.cpp)
set_target_properties(pf_face_probe PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_link_libraries(pf_face_probe PRIVATE pf::gpu Qt6::Core Qt6::Gui)
