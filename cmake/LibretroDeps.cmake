include(FetchContent)

# zstd - build from bundled source
set(ZSTD_BUILD_SHARED OFF)
set(ZSTD_BUILD_PROGRAMS OFF)
set(ZSTD_LEGACY_SUPPORT OFF)
FetchContent_Declare(zstd
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/vendor/libchdr/libchdr/deps/zstd-1.5.6/build/cmake
    OVERRIDE_FIND_PACKAGE
)
# Eagerly populate zstd so we can add the namespaced alias that libchdr expects.
# When built as a subdirectory the target is "libzstd_static", but find_package
# would provide "zstd::libzstd_static".
FetchContent_MakeAvailable(zstd)
if (TARGET libzstd_static AND NOT TARGET zstd::libzstd_static)
    add_library(zstd::libzstd_static ALIAS libzstd_static)
endif ()

# fmt - fetch from GitHub
set(FMT_DOC OFF)
set(FMT_TEST OFF)
FetchContent_Declare(fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 10.2.1
    OVERRIDE_FIND_PACKAGE
)
