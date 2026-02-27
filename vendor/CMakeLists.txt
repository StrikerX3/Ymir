# For developers:
#
# Try to keep vendored dependencies at a minimum to minimize bloat and make it easier for other
# developers to compile the code. Prefer including ports from vcpkg if possible, otherwise include
# them as submodules under this folder or the source tree if they're not git repos.
#
# If dependencies are included as submodules, explain why.
#
# find_package(...) can be used with any dependencies that misbehave (e.g. libraries that always
# include tests, or require tools that are unnecessary to consume it, like Python). Consider using
# a simpler alternative library, creating a custom CMakeLists.txt or manually building the target
# here from the source files.
# If you do add a library with find_package(...), list in the README so that other developers can
# provide them in the system to build this project.
#
# --------------------------------------------------------------------------------------------------

message(STATUS "Adding vendored libraries")

# mio - https://github.com/vimpunk/mio
# Notes:
# - Using StrikerX3's fork instead
#   - This fork implements the features in this PR: https://github.com/vimpunk/mio/pull/78
#   - It also stops the library from including CTest unconditionally, which is not done in that PR
message(STATUS "==> mio")
add_subdirectory(mio EXCLUDE_FROM_ALL)

# concurrentqueue - https://github.com/cameron314/concurrentqueue
# - Not available as a vcpkg port
message(STATUS "==> concurrentqueue")
add_subdirectory(concurrentqueue EXCLUDE_FROM_ALL)

# dear ImGui - https://github.com/ocornut/imgui
# - Using the docking branch
message(STATUS "==> dear ImGui")
add_subdirectory(imgui EXCLUDE_FROM_ALL)

# xxHash - https://github.com/Cyan4973/xxHash
# - Unable to configure AVX2 support
message(STATUS "==> xxHash")
add_subdirectory(xxHash EXCLUDE_FROM_ALL)

# lz4 - https://github.com/lz4/lz4
# - Unable to configure AVX2 support
message(STATUS "==> lz4")
add_subdirectory(lz4 EXCLUDE_FROM_ALL)

# libchdr - https://github.com/rtissera/libchdr
# - Not available as a vcpkg port
message(STATUS "==> libchdr")
add_subdirectory(libchdr)

# Place vendored library projects under a Vendored folder in Visual Studio solutions
if (MSVC)
    set_target_properties(chdr-static PROPERTIES FOLDER Vendored)
    set_target_properties(imgui PROPERTIES FOLDER Vendored)
    set_target_properties(lz4 PROPERTIES FOLDER Vendored)
    set_target_properties(lzma PROPERTIES FOLDER Vendored)
    set_target_properties(xxHash PROPERTIES FOLDER Vendored)
endif ()

message(STATUS "Done adding vendored libraries")
