# For developers:
#
# Try to keep vendored dependencies at a minimum to minimize bloat and make it easier for other
# developers to compile the code. Ideally they should be included as submodules under this folder.
#
# find_package(...) can be used with any dependencies that misbehave (e.g. libraries that always
# include tests, or require tools that are unnecessary to consume it, like Python). Consider using
# a simpler alternative library, creating a custom CMakeLists.txt or manually building the target
# here from the source files.
# If you do add a library with find_package(...), list in the README so that other developers can
# provide them in the system to build this project.
#
# --------------------------------------------------------------------------------------------------

## Helper function to silence messages from noisy subprojects
function(add_subdirectory_quiet DIR)
    set(PREV_CMAKE_MESSAGE_LOG_LEVEL ${CMAKE_MESSAGE_LOG_LEVEL})
    set(CMAKE_MESSAGE_LOG_LEVEL ERROR)
    add_subdirectory(${DIR} EXCLUDE_FROM_ALL)
    set(CMAKE_MESSAGE_LOG_LEVEL ${PREV_CMAKE_MESSAGE_LOG_LEVEL})
endfunction()

message(STATUS "Adding vendored libraries")

# mio - https://github.com/vimpunk/mio
# Notes:
# - Using StrikerX3's fork instead
#   - This fork implements the features in this PR: https://github.com/vimpunk/mio/pull/78
#   - It also stops the library from including CTest unconditionally, which is not done in that PR
message(STATUS "==> mio")
add_subdirectory(mio EXCLUDE_FROM_ALL)

# concurrentqueue - https://github.com/cameron314/concurrentqueue
message(STATUS "==> concurrentqueue")
add_subdirectory(concurrentqueue EXCLUDE_FROM_ALL)

# dear ImGui - https://github.com/ocornut/imgui
# docking branch
message(STATUS "==> dear ImGui")
add_subdirectory(imgui EXCLUDE_FROM_ALL)

# toml++ - https://github.com/marzer/tomlplusplus
message(STATUS "==> toml++")
add_subdirectory(tomlplusplus EXCLUDE_FROM_ALL)

# xxHash - https://github.com/Cyan4973/xxHash
message(STATUS "==> xxHash")
add_subdirectory(xxHash EXCLUDE_FROM_ALL)

# lz4 - https://github.com/lz4/lz4
message(STATUS "==> lz4")
add_subdirectory(lz4 EXCLUDE_FROM_ALL)

# stb - https://github.com/nothings/stb
message(STATUS "==> stb")
add_subdirectory(stb)

# libchdr - https://github.com/rtissera/libchdr
message(STATUS "==> libchdr")
set(BUILD_FUZZER OFF)
add_subdirectory(libchdr)

# Place vendored library projects under a Vendored folder in Visual Studio solutions
if (MSVC)
    set_target_properties(chdr-benchmark PROPERTIES FOLDER Vendored)
    set_target_properties(chdr-static PROPERTIES FOLDER Vendored)
    set_target_properties(imgui PROPERTIES FOLDER Vendored)
    set_target_properties(libzstd_static PROPERTIES FOLDER Vendored)
    set_target_properties(lz4 PROPERTIES FOLDER Vendored)
    set_target_properties(lzma PROPERTIES FOLDER Vendored)
    set_target_properties(stb PROPERTIES FOLDER Vendored)
    set_target_properties(xxHash PROPERTIES FOLDER Vendored)
    set_target_properties(zlibstatic PROPERTIES FOLDER Vendored)
endif ()

message(STATUS "Done adding vendored libraries")
