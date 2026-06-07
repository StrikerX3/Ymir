# compile_hlsl_shader(
#     OUT_VAR <variable>
#     INPUT_DIR <directory>
#     OUTPUT_DIR <directory>
#     SOURCE <filename>
#     ENTRY <entrypoint>
#     PROFILE <shader_profile>
#     [OUTPUT_NAME <filename>]
#     [VARIANT_NAME <name>]
#     [FLAGS <flag1> <flag2> ...]
# )
#
# Compiles a single HLSL shader using DXC and automatically tracks all file
# dependencies (including #includes) using DXC’s built‑in dependency generation.
#
# On Ninja/Makefile generators, the function passes DEPFILE to the build system
# for automatic tracking.
#
# On other generators (Visual Studio, Xcode, etc.), the .d file generated using
# DXC is parsed at configure time and the dependency list is fed into DEPENDS
# manually. For this reason, changes to shader include relationships (adding or
# removing #include directives) require reconfiguring the CMake project to
# update the dependency graph. Shader recompilation still works correctly when
# existing dependency files change.
#
# Parameters:
#
#   OUT_VAR
#       Name of a variable in the parent scope that will receive the full path
#       to the generated .cso file.
#
#   INPUT_DIR
#       Directory containing the shader source file.
#
#   OUTPUT_DIR
#       Directory where the compiled .cso and generated .d file will be written.
#
#   SOURCE
#       Name of the shader source file (e.g. "lighting_ps.hlsl"). This file is
#       resolved relative to INPUT_DIR.
#
#   ENTRY
#       Entry point function for DXC (e.g. "PSMain", "VSMain", "CSMain").
#
#   PROFILE
#       Shader profile to compile for (e.g. "ps_6_7", "vs_6_7", "cs_6_5").
#
#   OUTPUT_NAME (optional)
#       Overrides the base filename (without extension) of the generated .cso
#       and .d files. When provided, this value completely replaces the default
#       name derived from the SOURCE file.
#
#   VARIANT_NAME (optional)
#       Appends a suffix to the base filename (before the extension) to produce
#       a predictable, human‑readable variant name. This is useful when building
#       multiple versions of the same shader using different macro sets.
#
#   FLAGS (optional)
#       Additional DXC flags passed to both the dependency scan and the actual
#       compilation. This typically includes:
#         - include directories (-I)
#         - preprocessor defines (-D)
#         - optimization/debug flags
#       Using the same flags for both steps ensures dependency scanning respects
#       conditional includes and macro‑controlled code paths.
#
# Naming precedence:
#     If OUTPUT_NAME is provided, it takes full precedence and VARIANT_NAME is
#     ignored. Otherwise, VARIANT_NAME is appended to the base name.
#
#     Priority:
#         1. OUTPUT_NAME
#         2. BASE_NAME + "_" + VARIANT_NAME
#         3. BASE_NAME (default)
#
#     This ensures predictable filenames for runtime loading while still
#     supporting multiple prebuilt shader variants.
#
# Output:
#   OUT_VAR is set to the full path of the generated .cso file.
function(compile_hlsl_shader)
    set(options)
    set(oneValueArgs
        OUT_VAR
        INPUT_DIR
        OUTPUT_DIR
        SOURCE
        ENTRY
        PROFILE
        OUTPUT_NAME
        VARIANT_NAME
    )
    set(multiValueArgs
        FLAGS
    )

    cmake_parse_arguments(SHADER
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )

    get_filename_component(BASE_NAME "${SHADER_SOURCE}" NAME_WE)

    if(SHADER_OUTPUT_NAME)
        set(FINAL_NAME "${SHADER_OUTPUT_NAME}")
    elseif(SHADER_VARIANT_NAME)
        set(FINAL_NAME "${BASE_NAME}_${SHADER_VARIANT_NAME}")
    else()
        set(FINAL_NAME "${BASE_NAME}")
    endif()

    set(SRC_FILE "${SHADER_INPUT_DIR}/${SHADER_SOURCE}")
    set(DEP_FILE "${SHADER_OUTPUT_DIR}/${FINAL_NAME}.d")
    set(SHADER_OUTPUT "${SHADER_OUTPUT_DIR}/${FINAL_NAME}.cso")

    # DXC flags used for both dependency scan and compilation
    set(DXC_FLAGS
        -T "${SHADER_PROFILE}"
        -E "${SHADER_ENTRY}"
        ${SHADER_FLAGS}
    )

    if(CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
        # Use native DEPFILE support
        add_custom_command(
            OUTPUT ${SHADER_OUTPUT}

            # Preprocess-only pass to generate dependency file
            COMMAND dxc
                -MD -MF "${DEP_FILE}"
                ${DXC_FLAGS}
                "${SRC_FILE}"

            # Compile shader
            COMMAND dxc
                ${DXC_FLAGS}
                -Fo "${SHADER_OUTPUT}"
                "${SRC_FILE}"

            DEPFILE "${DEP_FILE}"
            COMMENT "Compiling ${SRC_FILE} to ${SHADER_OUTPUT}"
        )
    else()
        # Fallback path for Visual Studio, Xcode, etc.
        message(WARNING
            "compile_hlsl_shader: This generator does not support depfiles. "
            "Shader include dependency changes (adding/removing #include directives) "
            "will not be detected automatically. You must reconfigure the project "
            "after modifying include relationships."
        )

        # Generate dependency file now
        execute_process(
            COMMAND dxc -MD -MF "${DEP_FILE}" ${DXC_FLAGS} "${SRC_FILE}"
            RESULT_VARIABLE RES
            OUTPUT_QUIET
            ERROR_QUIET
        )

        if(NOT RES EQUAL 0)
            message(FATAL_ERROR "DXC failed while generating dependencies for ${SRC_FILE}")
        endif()

        # Parse dependency file
        file(READ "${DEP_FILE}" DEP_CONTENTS)
        string(REGEX REPLACE "^[^:]*:" "" DEPS "${DEP_CONTENTS}")
        string(REGEX REPLACE "[ \\\n]+" ";" DEPS "${DEPS}")
        list(FILTER DEPS EXCLUDE REGEX "^$")

        # Normalize paths
        set(AUTO_DEPS "")
        foreach(D ${DEPS})
            get_filename_component(ABS_D "${D}" ABSOLUTE)
            list(APPEND AUTO_DEPS "${ABS_D}")
        endforeach()

        add_custom_command(
            OUTPUT ${SHADER_OUTPUT}
            COMMAND dxc
                ${DXC_FLAGS}
                -Fo "${SHADER_OUTPUT}"
                "${SRC_FILE}"
            DEPENDS ${AUTO_DEPS}
            COMMENT "Compiling ${SRC_FILE} to ${SHADER_OUTPUT}"
        )
    endif()

    set(${SHADER_OUT_VAR} "${SHADER_OUTPUT}" PARENT_SCOPE)
endfunction()
