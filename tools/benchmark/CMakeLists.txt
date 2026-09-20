find_package(GTest CONFIG REQUIRED)
add_executable(pf_matcher_regression
    test_motion_matcher.cpp
    test_scene_detector.cpp
)
set_target_properties(pf_matcher_regression PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_link_libraries(pf_matcher_regression PRIVATE pf::core GTest::gtest_main)
add_test(NAME pf_matcher_regression COMMAND pf_matcher_regression)
