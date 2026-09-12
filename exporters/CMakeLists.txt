add_library(pfexporters STATIC
    src/Exporters.cpp
)
add_library(pf::exporters ALIAS pfexporters)
set_target_properties(pfexporters PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

target_include_directories(pfexporters PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(pfexporters PUBLIC cxx_std_23)
target_link_libraries(pfexporters PUBLIC pf::core)

# Qt-free like pfcore: export formats are pure text/JSON generation.
