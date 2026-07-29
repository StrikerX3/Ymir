add_library(clownresampler INTERFACE)
add_library(clownresampler::clownresampler ALIAS clownresampler)
target_include_directories(clownresampler INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")