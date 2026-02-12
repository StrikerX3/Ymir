add_library(ymir_libretro SHARED
    src/libretro.cpp
)

set_target_properties(ymir_libretro PROPERTIES
    OUTPUT_NAME "ymir_libretro"
    PREFIX ""
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(ymir_libretro PRIVATE
    include
)

target_link_libraries(ymir_libretro PRIVATE
    ymir::ymir-core
)

target_compile_features(ymir_libretro PUBLIC cxx_std_20)

if (NOT MSVC)
    target_compile_options(ymir_libretro PRIVATE -fvisibility=hidden)
endif ()

if (IPO_SUPPORTED AND Ymir_ENABLE_IPO)
    set_property(TARGET ymir_libretro PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif ()
