add_library(pfservices STATIC
    src/ThumbnailCache.cpp
)
add_library(pf::services ALIAS pfservices)
set_target_properties(pfservices PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

target_include_directories(pfservices PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(pfservices PUBLIC cxx_std_23)

# Note: pfservices is allowed to use Qt later (CutService needs QProcess),
# unlike pfcore/pfexporters which stay Qt-free.
