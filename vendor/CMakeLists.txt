# For developers:
#
# Try to keep vendored dependencies at a minimum to minimize bloat and make it easier for other
# developers to compile the code.
#
# Use vcpkg ports for complex dependencies. Prefer submodules or source trees (for projects without
# a Git URL) over vcpkg ports if they're relatively easy to include and compile. Create a custom
# CMakeLists.txt if the dependency does not behave well as a subproject (e.g. if it insists on
# creating executables, messes with global CMake variables, or requires additional tools to be
# installed just to build targets other than the library itself).
#
# DO NOT USE vcpkg ports for ymir-core dependencies to ensure the project can be used as a vendored
# dependency cleanly and easily by other CMake projects.
#
# Group vendored libraries used by ymir-core in the designated section below.
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

####################################################################################################
# Dependencies used by ymir-core

# mio - https://github.com/vimpunk/mio
# - Using StrikerX3's fork instead
#   - This fork implements the features in this PR: https://github.com/vimpunk/mio/pull/78
#   - It also stops the library from including CTest unconditionally, which is not done in that PR
message(STATUS "==> mio")
add_subdirectory(mio EXCLUDE_FROM_ALL)

# {fmt} - https://github.com/fmtlib/fmt
message(STATUS "==> libfmt")
set(FMT_MODULE OFF)
add_subdirectory(fmt)

# concurrentqueue - https://github.com/cameron314/concurrentqueue
message(STATUS "==> concurrentqueue")
add_subdirectory(concurrentqueue EXCLUDE_FROM_ALL)

# xxHash - https://github.com/Cyan4973/xxHash
message(STATUS "==> xxHash")
add_subdirectory(xxHash EXCLUDE_FROM_ALL)

# libchdr - https://github.com/rtissera/libchdr
message(STATUS "==> libchdr")
add_subdirectory(libchdr)

####################################################################################################
# Other dependencies

if (NOT Ymir_LIBRARY_ONLY)
    # dear ImGui - https://github.com/ocornut/imgui
    # - Using the docking branch
    message(STATUS "==> dear ImGui")
    add_subdirectory(imgui EXCLUDE_FROM_ALL)

    # lz4 - https://github.com/lz4/lz4
    # - Unable to configure AVX2 support from vcpkg
    message(STATUS "==> lz4")
    add_subdirectory(lz4 EXCLUDE_FROM_ALL)
endif ()

# Place vendored library projects under a Vendored folder in Visual Studio solutions
if (MSVC)
    set_target_properties(chdr-lzma PROPERTIES FOLDER Vendored)
    set_target_properties(chdr-static PROPERTIES FOLDER Vendored)
    set_target_properties(xxHash PROPERTIES FOLDER Vendored)
    if (NOT Ymir_LIBRARY_ONLY)
        set_target_properties(imgui PROPERTIES FOLDER Vendored)
        set_target_properties(lz4 PROPERTIES FOLDER Vendored)
    endif ()
endif ()

message(STATUS "Done adding vendored libraries")
