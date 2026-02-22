add_library(smol-atlas
    smol-atlas/src/smol-atlas.cpp
    smol-atlas/src/smol-atlas.h
)

add_library(smol-atlas::smol-atlas ALIAS smol-atlas)
target_include_directories(smol-atlas
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/smol-atlas/src>"
    PRIVATE
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/smol-atlas/src>"
)
target_compile_features(smol-atlas PUBLIC cxx_std_20)

## Apply performance options
if (Ymir_AVX2)
    if (MSVC)
        target_compile_options(smol-atlas PUBLIC "/arch:AVX2")
    else ()
        target_compile_options(smol-atlas PUBLIC "-mavx2" "-mfma" "-mpopcnt" "-mlzcnt" "-mbmi" "-mbmi2")
    endif ()
endif ()
