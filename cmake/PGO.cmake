set(_Ymir_PGO_MODULE_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(ymir_configure_pgo)
    # PGO control (tri-state)
    set(Ymir_PGO "OFF" CACHE STRING "Enable PGO (OFF, GENERATE, USE)")
    set_property(CACHE Ymir_PGO PROPERTY STRINGS OFF GENERATE USE)

    # Profile data locations
    set(Ymir_PGO_DIR "${CMAKE_BINARY_DIR}/pgo-profdata" CACHE PATH "PGO profile data directory")
    set(Ymir_PGO_PROFDATA "${Ymir_PGO_DIR}/ymir.profdata" CACHE FILEPATH "Merged LLVM PGO profile data")

    # Validate user input
    string(TOUPPER "${Ymir_PGO}" _ymir_pgo_mode)
    if (NOT _ymir_pgo_mode STREQUAL "OFF" AND
        NOT _ymir_pgo_mode STREQUAL "GENERATE" AND
        NOT _ymir_pgo_mode STREQUAL "USE")
        message(FATAL_ERROR "Invalid Ymir_PGO value: ${Ymir_PGO}. Valid values: OFF, GENERATE, USE.")
    endif ()

    if (_ymir_pgo_mode STREQUAL "OFF")
        return()
    endif ()

    file(MAKE_DIRECTORY "${Ymir_PGO_DIR}")
    message(STATUS "Ymir: PGO ${_ymir_pgo_mode}")
    message(STATUS "Ymir: PGO profile dir ${Ymir_PGO_DIR}")

    # LLVM PGO (Clang / AppleClang) – recommended on macOS + generally great everywhere.
    if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        if (_ymir_pgo_mode STREQUAL "GENERATE")
            add_compile_options(-fprofile-instr-generate)
            add_link_options(-fprofile-instr-generate)

            # Helper target to merge profraw -> profdata.
            find_program(LLVM_PROFDATA_EXE llvm-profdata)
            if (LLVM_PROFDATA_EXE)
                add_custom_target(ymir-pgo-merge
                    COMMAND ${CMAKE_COMMAND}
                        -D PGO_DIR="${Ymir_PGO_DIR}"
                        -D PGO_PROFDATA="${Ymir_PGO_PROFDATA}"
                        -D LLVM_PROFDATA_EXE="${LLVM_PROFDATA_EXE}"
                        -P "${_Ymir_PGO_MODULE_DIR}/PGOMerge.cmake"
                    BYPRODUCTS "${Ymir_PGO_PROFDATA}"
                    COMMENT "Merging LLVM .profraw into ${Ymir_PGO_PROFDATA}"
                    VERBATIM
                )
            else ()
                message(WARNING "llvm-profdata not found; you will need it to merge .profraw -> .profdata")
            endif ()
        elseif (_ymir_pgo_mode STREQUAL "USE")
            if (NOT EXISTS "${Ymir_PGO_PROFDATA}")
                message(FATAL_ERROR "Ymir_PGO=USE but profdata not found: ${Ymir_PGO_PROFDATA}")
            endif ()

            add_compile_options("-fprofile-instr-use=${Ymir_PGO_PROFDATA}")
            add_link_options("-fprofile-instr-use=${Ymir_PGO_PROFDATA}")
            add_compile_options(-Wno-profile-instr-unprofiled -Wno-profile-instr-out-of-date)
        endif ()

    # GCC PGO (Linux)
    elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        if (_ymir_pgo_mode STREQUAL "GENERATE")
            add_compile_options("-fprofile-generate=${Ymir_PGO_DIR}/gcc")
            add_link_options("-fprofile-generate=${Ymir_PGO_DIR}/gcc")
        elseif (_ymir_pgo_mode STREQUAL "USE")
            add_compile_options("-fprofile-use=${Ymir_PGO_DIR}/gcc" -fprofile-correction)
            add_link_options("-fprofile-use=${Ymir_PGO_DIR}/gcc" -fprofile-correction)
        endif ()
    else ()
        message(WARNING "PGO requested, but compiler ${CMAKE_CXX_COMPILER_ID} is not handled in cmake/PGO.cmake")
    endif ()
endfunction()
