add_library(miniaudio INTERFACE)
add_library(miniaudio::miniaudio ALIAS miniaudio)
target_include_directories(miniaudio INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")