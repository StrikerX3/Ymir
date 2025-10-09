## Create the executable target
add_executable(ymdasm
    src/ansi.hpp
    src/colors.hpp
    src/disassembler.hpp
    src/disassembler_m68k.cpp
    src/disassembler_m68k.hpp
    src/disassembler_scspdsp.cpp
    src/disassembler_scspdsp.hpp
    src/disassembler_scudsp.cpp
    src/disassembler_scudsp.hpp
    src/disassembler_sh2.cpp
    src/disassembler_sh2.hpp
    src/fetcher.hpp
    src/utils.hpp
    src/ymdasm.cpp
)
add_executable(ymir::ymdasm ALIAS ymdasm)
set_target_properties(ymdasm PROPERTIES
                      VERSION ${Ymir_VERSION}
                      SOVERSION ${Ymir_VERSION_MAJOR})
target_include_directories(ymdasm
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    PRIVATE "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
)

find_package(cxxopts CONFIG REQUIRED)

target_link_libraries(ymdasm PRIVATE
    ymir::ymir-core
    cxxopts::cxxopts
)
target_compile_features(ymdasm PUBLIC cxx_std_20)

cmrk_copy_runtime_dlls(ymdasm)

if (IPO_SUPPORTED AND Ymir_ENABLE_IPO)
    message(STATUS "Enabling IPO / LTO for ymdasm")
    set_property(TARGET ymdasm PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

## Apply performance options
if (Ymir_AVX2)
    if (MSVC)
        target_compile_options(ymdasm PUBLIC "/arch:AVX2")
    else ()
        target_compile_options(ymdasm PUBLIC "-mavx2")
        target_compile_options(ymdasm PUBLIC "-mfma")
        target_compile_options(ymdasm PUBLIC "-mbmi")
    endif ()
endif ()

## Configure Visual Studio solution
if (MSVC)
    vs_set_filters(TARGET ymdasm)
    set_target_properties(ymdasm PROPERTIES FOLDER "Ymir")
endif ()

## Include the install rules if requested
if (Ymir_INCLUDE_PACKAGING)
    add_subdirectory(packaging)
endif ()
