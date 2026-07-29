add_library(speexdsp OBJECT
    speexdsp/libspeexdsp/resample.c
)

add_library(speexdsp::speexdsp ALIAS speexdsp)

# Include path setup
target_include_directories(speexdsp PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/speexdsp/include>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/speexdsp/include/speex>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/speexdsp/libspeexdsp>"
)

target_compile_definitions(speexdsp PUBLIC
    OUTSIDE_SPEEX
    RANDOM_PREFIX=ymir_speex
    FLOATING_POINT
    EXPORT=
)