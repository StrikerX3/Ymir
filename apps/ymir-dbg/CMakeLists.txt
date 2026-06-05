## Create the executable target
add_executable(ymir-dbg
    src/main.cpp
)
add_executable(ymir::ymir-dbg ALIAS ymir-dbg)
set_target_properties(ymir-dbg PROPERTIES
                      VERSION ${Ymir_VERSION}
                      SOVERSION ${Ymir_VERSION_MAJOR})
target_include_directories(ymir-dbg
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    PRIVATE "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
)
target_compile_features(ymir-dbg PUBLIC cxx_std_20)

find_package(cxxopts CONFIG REQUIRED)

target_link_libraries(ymir-dbg PRIVATE
    ymir::ymir-dbg-commons
    cxxopts::cxxopts
    fmt::fmt
)

cmrk_copy_runtime_dlls(ymir-dbg)

if (IPO_SUPPORTED AND Ymir_ENABLE_IPO)
    message(STATUS "Enabling IPO / LTO for ymir-dbg")
    set_property(TARGET ymir-dbg PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

## Apply performance options
if (Ymir_AVX2)
    if (MSVC)
        target_compile_options(ymir-dbg PUBLIC "/arch:AVX2")
    else ()
        target_compile_options(ymir-dbg PUBLIC "-mavx2" "-mfma" "-mpopcnt" "-mlzcnt" "-mbmi" "-mbmi2")
    endif ()
endif ()

## Configure Visual Studio solution
if (MSVC)
    vs_set_filters(TARGET ymir-dbg)
    set_target_properties(ymir-dbg PROPERTIES FOLDER "Ymir")
endif ()
