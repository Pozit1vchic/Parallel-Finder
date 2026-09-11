add_library(pfcore STATIC
    src/SceneDetector.cpp
    src/VideoDecoder.cpp
    src/MotionMatcher.cpp
)
add_library(pf::core ALIAS pfcore)
set_target_properties(pfcore PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

target_include_directories(pfcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(pfcore PUBLIC cxx_std_23)

# Guard: pfcore must not include Qt headers (spec section 2: "ядро без Qt").
set(PF_CORE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pfcore/SceneDetector.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pfcore/VideoDecoder.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SceneDetector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/VideoDecoder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pfcore/MotionMatcher.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/MotionMatcher.cpp
)

find_path(PF_FFMPEG_INCLUDE_DIR NAMES libavformat/avformat.h REQUIRED)
find_library(PF_AVFORMAT_LIBRARY NAMES avformat REQUIRED)
find_library(PF_AVCODEC_LIBRARY NAMES avcodec REQUIRED)
find_library(PF_AVUTIL_LIBRARY NAMES avutil REQUIRED)
find_library(PF_SWSCALE_LIBRARY NAMES swscale REQUIRED)
target_include_directories(pfcore PRIVATE ${PF_FFMPEG_INCLUDE_DIR})
target_link_libraries(pfcore PRIVATE ${PF_AVFORMAT_LIBRARY} ${PF_AVCODEC_LIBRARY}
    ${PF_AVUTIL_LIBRARY} ${PF_SWSCALE_LIBRARY})
# Run the Qt ban at configure time. A nested cmd.exe custom target can remain
# blocked after an interrupted Ninja invocation on Windows.
foreach(_pf_file IN LISTS PF_CORE_SOURCES)
    file(READ "${_pf_file}" _pf_content)
    if(_pf_content MATCHES [[#include[ \t]*[<"]Qt]])
        message(FATAL_ERROR "pfcore must remain Qt-free: ${_pf_file}")
    endif()
endforeach()
