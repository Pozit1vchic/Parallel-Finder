add_library(pfgpu STATIC
    src/DeviceInfo.cpp
    src/OrtRuntime.cpp
    src/ProbeModel.cpp
    src/Provider.cpp
    src/ProviderFactory.cpp
    src/SessionCache.cpp
    src/Inference.cpp
    src/PoseEstimator.cpp
)
add_library(pf::gpu ALIAS pfgpu)
set_target_properties(pfgpu PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

target_include_directories(pfgpu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(pfgpu PUBLIC cxx_std_23)

# ORT is intentionally NOT link-time dependent: pfgpu uses LoadLibraryW +
# OrtGetApiBase with a descending OrtApi version probe, so one binary drives
# whatever runtime the user has (Base bundle or the GPU bundle in D:\PF_CUDA).
# Headers are needed at build time only, for the types.
#
# The header layout differs between distributions: the MSYS2 package nests it as
# include/onnxruntime/onnxruntime_c_api.h, the official archives ship it flat in
# include/. PATH_SUFFIXES covers both.
find_path(PF_ORT_INCLUDE_DIR
    NAMES onnxruntime_c_api.h
    PATH_SUFFIXES onnxruntime
    REQUIRED
)
message(STATUS "pfgpu: ONNX Runtime headers: ${PF_ORT_INCLUDE_DIR}")
target_include_directories(pfgpu PUBLIC ${PF_ORT_INCLUDE_DIR})

# DXGI gives human-readable adapter names for the GPU badge. Absent on non
# Windows hosts; the code degrades to provider names.
if(WIN32)
    find_path(PF_DXGI_INCLUDE_DIR NAMES dxgi.h)
    find_library(PF_DXGI_LIBRARY NAMES dxgi)
    if(PF_DXGI_INCLUDE_DIR AND PF_DXGI_LIBRARY)
        target_compile_definitions(pfgpu PRIVATE PFGPU_HAS_DXGI=1)
        target_link_libraries(pfgpu PRIVATE ${PF_DXGI_LIBRARY})
    endif()
endif()
