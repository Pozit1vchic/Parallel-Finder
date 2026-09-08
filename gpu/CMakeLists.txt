add_library(pfgpu STATIC
    src/DeviceInfo.cpp
)
add_library(pf::gpu ALIAS pfgpu)

target_include_directories(pfgpu PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(pfgpu PUBLIC cxx_std_23)

# ORT is intentionally NOT link-time dependent: the release loads
# onnxruntime.dll dynamically (LoadLibraryW + OrtGetApiBase) so the runtime is
# version-agnostic and the Base bundle ships without GPU providers.
# Stage 6 adds the GPU bundle (ORT CUDA/TensorRT redists) in D:\PF_CUDA.
