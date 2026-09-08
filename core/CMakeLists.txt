add_library(pfcore STATIC
    src/SceneDetector.cpp
)
add_library(pf::core ALIAS pfcore)

target_include_directories(pfcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(pfcore PUBLIC cxx_std_23)

# Guard: pfcore must not include Qt headers (spec section 2: "ядро без Qt").
file(GLOB_RECURSE PF_CORE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)
add_custom_target(pfcore_qt_ban
    COMMAND ${CMAKE_COMMAND}
        -DQT_BAN_FILES="${PF_CORE_SOURCES}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/QtBanCheck.cmake
    DEPENDS ${PF_CORE_SOURCES}
    VERBATIM
)
add_dependencies(pfcore pfcore_qt_ban)
