add_library(pfexporters INTERFACE)
add_library(pf::exporters ALIAS pfexporters)
set_target_properties(pfexporters PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)

target_include_directories(pfexporters INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(pfexporters INTERFACE cxx_std_23)

# Qt-free like pfcore: export formats are pure text/JSON generation.
