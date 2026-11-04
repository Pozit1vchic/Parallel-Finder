add_library(pfcore STATIC
    src/FaceAssociation.cpp
    src/FaceConsensus.cpp
    src/SceneSequenceCache.cpp
    src/SceneDetector.cpp
    src/SceneWindow.cpp
    src/VideoDecoder.cpp
    src/VideoSampleReader.cpp
    src/MotionMatcher.cpp
    src/MotionIndex.cpp
    src/DominantPerson.cpp
    src/RepeatedSceneIdentity.cpp
    src/JobManager.cpp
    src/MovementClassifier.cpp
    src/MovementGrouping.cpp
    src/HeadOrientation.cpp
    src/BodyOrientation.cpp
    src/MotionRanker.cpp
    src/SceneDescriptor.cpp
    src/SceneFamily.cpp
    src/ResultOrganization.cpp
)
add_library(pf::core ALIAS pfcore)
set_target_properties(pfcore PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

target_include_directories(pfcore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(pfcore PUBLIC cxx_std_23)

# Guard: pfcore must not include Qt headers (spec section 2: "ядро без Qt").
set(PF_CORE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pfcore/DetectorOutputReplay.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pfcore/BodyOrientationSample.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/BodyOrientation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pfcore/FaceAssociation.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/FaceAssociation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pfcore/SceneDescriptor.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pfcore/SceneFamily.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pfcore/SceneFamilyEvidence.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pfcore/OrganizedClip.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SceneDescriptor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SceneFamily.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ResultOrganization.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pfcore/VisualObjectEvidence.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pfcore/PoseAssociation.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pfcore/SceneWindow.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SceneWindow.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pfcore/SceneSequenceCache.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SceneSequenceCache.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pfcore/SceneDetector.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pfcore/VideoDecoder.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SceneDetector.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/VideoDecoder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pfcore/VideoSampleReader.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/VideoSampleReader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pfcore/MotionMatcher.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/MotionMatcher.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pfcore/MotionIndex.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/MotionIndex.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pfcore/DominantPerson.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/DominantPerson.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pfcore/RepeatedSceneIdentity.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/RepeatedSceneIdentity.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pfcore/JobManager.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/JobManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pfcore/MovementClassifier.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/MovementClassifier.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/MovementGrouping.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pfcore/HeadOrientation.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/HeadOrientation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/include/pfcore/MotionRanker.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/MotionRanker.cpp
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
