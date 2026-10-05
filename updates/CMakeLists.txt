add_library(pfupdate STATIC
    include/pfupdate/UpdateService.hpp
    src/UpdateManifest.cpp src/UpdateTransaction.cpp src/UpdateService.cpp)
target_include_directories(pfupdate PUBLIC include)
target_link_libraries(pfupdate PUBLIC Qt6::Core Qt6::Network OpenSSL::Crypto LibArchive::LibArchive)
file(READ "${CMAKE_SOURCE_DIR}/tools/release/update-public-key.pem" PF_UPDATE_PUBLIC_KEY)
configure_file(src/UpdateTrust.hpp.in ${CMAKE_CURRENT_BINARY_DIR}/UpdateTrust.hpp @ONLY)
target_include_directories(pfupdate PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
add_executable(ParallelFinderUpdater src/updater_main.cpp)
target_link_libraries(ParallelFinderUpdater PRIVATE pfupdate)
if(WIN32)
    target_link_libraries(ParallelFinderUpdater PRIVATE advapi32)
endif()
