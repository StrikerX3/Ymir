add_library(dr_libs INTERFACE)
add_library(dr_libs::dr_libs ALIAS dr_libs)
target_include_directories(dr_libs INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include/")
