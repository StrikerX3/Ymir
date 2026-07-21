## Create the executable target
add_executable(ymir-sandbox
    src/main.cpp
    src/stb_implementations.cpp

    src/sandbox_bin_cue_loader.cpp
    src/sandbox_bup.cpp
    src/sandbox_curl.cpp
    src/sandbox_deadlock.cpp
    src/sandbox_disc_info_extractor.cpp
    src/sandbox_gpu.cpp
    src/sandbox_host_cd.cpp
    src/sandbox_input.cpp
    src/sandbox_sh2_perf.cpp
    src/sandbox_vdp1_accuracy.cpp
    src/sandbox_vdp1_poly.cpp
    src/sandbox_vulkan.cpp
)
add_executable(ymir::ymir-sandbox ALIAS ymir-sandbox)
set_target_properties(ymir-sandbox PROPERTIES
                      VERSION ${Ymir_VERSION}
                      SOVERSION ${Ymir_VERSION_MAJOR})
target_include_directories(ymir-sandbox
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    PRIVATE "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
)
target_compile_features(ymir-sandbox PUBLIC cxx_std_20)

if (WIN32)
    target_sources(ymir-sandbox PRIVATE
        src/d3d12_graphics_context.hpp

        src/sandbox_d3d12_windows.cpp
    )
    target_compile_definitions(ymir-sandbox PRIVATE WIN32_LEAN_AND_MEAN)
    target_compile_definitions(ymir-sandbox PRIVATE NOMINMAX)
    target_link_libraries(ymir-sandbox PRIVATE
        WIL::WIL
    )
else ()
    target_sources(ymir-sandbox PRIVATE
        src/sandbox_d3d12_other.cpp
    )
endif ()

## Compile shaders
set(SHADER_INPUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/res/shaders")
set(SHADER_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/shaders")

set(COMPUTE_SHADER_SOURCES
    cs_flipbits.hlsl
)

set(COMPILED_SHADERS "")
foreach (SHADER_SRC ${COMPUTE_SHADER_SOURCES})
    compile_hlsl_shader(
        OUT_VAR SHADER_CSO
        INPUT_DIR "${SHADER_INPUT_DIR}"
        OUTPUT_DIR "${SHADER_OUTPUT_DIR}"
        SOURCE "${SHADER_SRC}"
        ENTRY "CSMain"
        PROFILE "cs_6_5"
        FLAGS -Qstrip_debug -Qstrip_reflect -Zi
    )
    list(APPEND COMPILED_SHADERS ${SHADER_CSO})
endforeach ()

compile_hlsl_shader(
    OUT_VAR SHADER_CSO
    INPUT_DIR "${SHADER_INPUT_DIR}"
    OUTPUT_DIR "${SHADER_OUTPUT_DIR}"
    SOURCE "cs_flipbits.hlsl"
    ENTRY "CSMain"
    PROFILE "cs_6_5"
    VARIANT_NAME "with_include"
    FLAGS -Qstrip_debug -Qstrip_reflect -Zi -DUSE_INCLUDE=1
)
list(APPEND COMPILED_SHADERS ${SHADER_CSO})

## Create the embedded resources target
MESSAGE(STATUS "Generating shader resource library: ${COMPILED_SHADERS}")
cmrc_add_resource_library(ymir-sandbox-shaders
    ALIAS ymir-sandbox::shaders
    NAMESPACE ymir_shaders
    WHENCE "${SHADER_OUTPUT_DIR}"
    ${COMPILED_SHADERS}
)

find_package(date CONFIG REQUIRED)
find_package(CURL CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(SDL3 CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(Vulkan REQUIRED)

## Add dependencies
target_link_libraries(ymir-sandbox PRIVATE
    ymir-sandbox-shaders
    ymir::ymir-core
    SDL3::SDL3
    date::date
    fmt::fmt
    CURL::libcurl_static
    nlohmann_json::nlohmann_json
    imgui::imgui
    Vulkan::Vulkan
)
target_include_directories(ymir-sandbox PRIVATE ${Stb_INCLUDE_DIR})

cmrk_copy_runtime_dlls(ymir-sandbox)

if (IPO_SUPPORTED AND Ymir_ENABLE_IPO)
    message(STATUS "Enabling IPO / LTO for ymir-sandbox")
    set_property(TARGET ymir-sandbox PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

## Apply performance options
if (Ymir_AVX2)
    if (MSVC)
        target_compile_options(ymir-sandbox PUBLIC "/arch:AVX2")
    else ()
        target_compile_options(ymir-sandbox PUBLIC "-mavx2" "-mfma" "-mpopcnt" "-mlzcnt" "-mbmi" "-mbmi2")
    endif ()
endif ()

## Configure Visual Studio solution
if (MSVC)
    vs_set_filters(TARGET ymir-sandbox)
    set_target_properties(ymir-sandbox PROPERTIES FOLDER "Ymir")
endif ()

## No packaging for this project as it's meant to be testing grounds
