add_executable(bus-trace-convert
    src/main.cpp
)
add_executable(ymir::bus-trace-convert ALIAS bus-trace-convert)

set_target_properties(bus-trace-convert PROPERTIES
                      VERSION ${Ymir_VERSION}
                      SOVERSION ${Ymir_VERSION_MAJOR})

target_link_libraries(bus-trace-convert PRIVATE
    ymir::ymir-core
)

target_compile_features(bus-trace-convert PUBLIC cxx_std_20)

if (IPO_SUPPORTED AND Ymir_ENABLE_IPO)
    message(STATUS "Enabling IPO / LTO for bus-trace-convert")
    set_property(TARGET bus-trace-convert PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

if (MSVC)
    vs_set_filters(TARGET bus-trace-convert)
    set_target_properties(bus-trace-convert PROPERTIES FOLDER "Ymir")
endif ()
