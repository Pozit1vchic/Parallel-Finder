add_library(pfservices STATIC
    src/ThumbnailCache.cpp
    src/SettingsStore.cpp
    src/ModelStore.cpp
    src/ProviderStore.cpp
    src/CutService.cpp
    src/ExportQueue.cpp
    src/MontageExport.cpp
    src/RuntimeScratch.cpp
    src/FfmpegCapabilities.cpp
    src/ProviderManager.cpp
    src/PfCache.cpp
    src/MatchCache.cpp
    src/CachedPreview.cpp
    src/PreviewWriter.cpp
)
add_library(pf::services ALIAS pfservices)
set_target_properties(pfservices PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

target_include_directories(pfservices PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(pfservices PUBLIC cxx_std_23)
target_link_libraries(pfservices PUBLIC Qt6::Core Qt6::Network Qt6::Gui pf::core)

# A rebuilt matcher must not reuse final results from different algorithm
# code, even when a developer keeps the same release label. Content changes
# trigger reconfiguration; paths in the digest are relative for reproducibility.
file(GLOB_RECURSE _pf_match_contract_files CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/core/include/*.hpp" "${PROJECT_SOURCE_DIR}/core/src/*.cpp"
    "${PROJECT_SOURCE_DIR}/core/src/*.hpp")
list(APPEND _pf_match_contract_files "${CMAKE_CURRENT_SOURCE_DIR}/src/MatchCache.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/pfservices/MatchCache.hpp"
    "${PROJECT_SOURCE_DIR}/CMakeLists.txt" "${PROJECT_SOURCE_DIR}/core/CMakeLists.txt")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_pf_match_contract_files})
set(_pf_match_contract "${CMAKE_CXX_COMPILER_ID}|${CMAKE_CXX_COMPILER_VERSION}|${CMAKE_SYSTEM_PROCESSOR}|${CMAKE_SIZEOF_VOID_P}|${CMAKE_CXX_FLAGS}|${CMAKE_CXX_FLAGS_RELEASE}")
foreach(_pf_contract_file IN LISTS _pf_match_contract_files)
    file(SHA256 "${_pf_contract_file}" _pf_contract_hash)
    file(RELATIVE_PATH _pf_contract_path "${PROJECT_SOURCE_DIR}" "${_pf_contract_file}")
    string(APPEND _pf_match_contract "|${_pf_contract_path}:${_pf_contract_hash}")
endforeach()
string(SHA256 _pf_match_contract_hash "${_pf_match_contract}")
target_compile_definitions(pfservices PRIVATE PF_MATCHER_CONTRACT="${_pf_match_contract_hash}-$<CONFIG>")

# Note: pfservices is allowed to use Qt later (CutService needs QProcess),
# unlike pfcore/pfexporters which stay Qt-free.
