qt_add_executable(ParallelFinder
    src/main.cpp
    ParallelFinder.rc
)

target_compile_definitions(ParallelFinder PRIVATE PF_VERSION="${PF_RELEASE_LABEL}")
target_link_libraries(ParallelFinder PRIVATE
    pfui               # QML module PfUi (static): Theme/L10n/Main + AppInfo
    pfui_module_import # static import of the PfUi module plugin
    pfgpu              # init step reads the active backend for the GPU badge
)

set_target_properties(ParallelFinder PROPERTIES
    MACOSX_BUNDLE FALSE
    WIN32_EXECUTABLE TRUE
)

# Keep development analysis consistent with the packaged identity pipeline.
set(PF_IDENTITY_MODEL_DIRECTORY "${PROJECT_SOURCE_DIR}/build/release-base-models" CACHE PATH
    "Directory containing the identity models staged by the release tooling")
foreach(identity_asset person-reid-osnet.onnx face_detection_yunet_2023mar.onnx
    face_recognition_sface_2021dec.onnx face_detection_yunet.LICENSE face_recognition_sface.LICENSE)
    if(EXISTS "${PF_IDENTITY_MODEL_DIRECTORY}/${identity_asset}")
        add_custom_command(TARGET ParallelFinder POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:ParallelFinder>/models"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${PF_IDENTITY_MODEL_DIRECTORY}/${identity_asset}"
                "$<TARGET_FILE_DIR:ParallelFinder>/models/${identity_asset}")
    endif()
endforeach()
