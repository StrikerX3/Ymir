################################################################################
## Find required tools.
##
## Targets:
##   Direct3D 12 - DXIL
##   Vulkan - SPIR-V
##   Metal - SPIR-V -> MetalLib
##
## Requires either DXC (DirectX Shader Compiler) or shaderc.
## On Windows, DXC is mandatory as Direct3D 12 can only consume DXIL shaders.
## On Linux and macOS, either DXC or shaderc are accepted.
##
## When locating DXC, this script prefers the compiler included with Vulkan SDK
## as it supports SPIR-V. It falls back to system-provided DXC otherwise, such
## as Visual Studio's DXC which only targets DXIL (in which case the lack of
## SPIR-V support doesn't matter).
##
## On macOS, DXC with SPIR-V support or shaderc is required for the first
## compilation step. Later steps require metal and metallib.
##
## Outputs:
##   DXC_EXECUTABLE (STRING): path to DXC executable
##   DXC_DXIL_SUPPORTED (BOOL): whether the DXC executable can output DXIL
##   DXC_SPIRV_SUPPORTED (BOOL): whether the DXC executable can output SPIR-V
##   SHADERC_EXECUTABLE (STRING): path to shaderc executable
##   SHADERS_SPIRV_SUPPORTED_BY (STRING): name of the shader that will be used
##     to compile shaders to SPIR-V, either "DXC" or "shaderc". Blank string if
##     SPIR-V shader compilation is not supported.

# Try to locate DXC in Vulkan SDK
find_program(DXC_EXECUTABLE_VULKAN
    NAMES dxc
    HINTS "$ENV{VULKAN_SDK}/bin" "$ENV{VULKAN_SDK}/bin64"
    NO_DEFAULT_PATH
)

# If found, we know it has SPIR-V and DXIL support.
# Enable DXIL on Windows only (as it only makes sense there), and SPIR-V everywhere.
set(DXC_DXIL_SUPPORTED ${WIN32})
set(DXC_SPIRV_SUPPORTED OFF)
if (DXC_EXECUTABLE_VULKAN)
    set(DXC_EXECUTABLE ${DXC_EXECUTABLE_VULKAN})
    set(DXC_SPIRV_SUPPORTED ON)
endif ()

#### TODO: might need additional checks on macOS to ensure DXC actually has SPIR-V support

# Fall back to system-provided DXC compiler
if (NOT DXC_EXECUTABLE)
    find_program(DXC_EXECUTABLE NAMES dxc)
endif ()

# Bail out right away on Windows as we cannot easily compile DXIL shaders without DXC.
if (WIN32 AND NOT DXC_EXECUTABLE)
    message(FATAL_ERROR "Could NOT find DXC. Cannot compile DXIL shaders.")
endif ()

# Try shaderc if DXC cannot be found or doesn't support SPIR-V.
# Also search in Vulkan SDK for consistency and convenience.
if (NOT DXC_EXECUTABLE OR NOT DXC_SPIRV_SUPPORTED)
    find_program(SHADERC_EXECUTABLE
        NAMES glslc
        HINTS "$ENV{VULKAN_SDK}/bin" "$ENV{VULKAN_SDK}/bin64"
    )
endif ()

# Bail out if neither executable could be found
if (NOT DXC_EXECUTABLE AND NOT SHADERC_EXECUTABLE)
    message(FATAL_ERROR "Could NOT find DXC nor shaderc. Cannot compile shaders.")
endif ()

if (APPLE)
    ## TODO: find metal and metallib on macOS, bail out if they cannot be found
endif ()

# Check SPIR-V support
set(SHADERS_SPIRV_SUPPORTED_BY "")
if (DXC_SPIRV_SUPPORTED)
    set(SHADERS_SPIRV_SUPPORTED_BY "DXC")
elseif (SHADERC_EXECUTABLE)
    set(SHADERS_SPIRV_SUPPORTED_BY "shaderc")
endif ()

# SPIR-V is required on systems other than Windows. Bail out if that's not the case.
if (NOT WIN32 AND NOT SHADERS_SPIRV_SUPPORTED_BY)
    message(FATAL_ERROR "Could NOT find a shader compiler supporting SPIR-V. Cannot compile shaders.")
endif ()

# Output status
if (DXC_EXECUTABLE)
    message(STATUS "DXC found: ${DXC_EXECUTABLE}")
    if (DXC_DXIL_SUPPORTED)
        message(STATUS "DXC supports DXIL shaders")
    endif ()
    if (DXC_SPIRV_SUPPORTED)
        message(STATUS "DXC supports SPIR-V shaders")
    endif ()
endif ()
if (SHADERC_EXECUTABLE)
    message(STATUS "shaderc found: ${SHADERC_EXECUTABLE}")
endif ()
if (SHADERS_SPIRV_SUPPORTED_BY)
    message(STATUS "${SHADERS_SPIRV_SUPPORTED_BY} will be used to compile shaders to SPIR-V")
endif ()

################################################################################
## Compiler function

# compile_shader(
#     OUT_PATHS <variable>
#     SOURCE <path_to_hlsl>
#     [WHENCE <base_path>]
#     DESTINATION <directory>
#     ENTRYPOINT <string>
#     PROFILE <string>
#     [VARIANT <string>]
#     [MACROS <macro1> <macro2> ...]
#     [INCLUDE_REFLECTION]
# )
#
# Configures a custom target to build a single HLSL shader with DXC, producing
# DXIL and/or SPIR-V bytecode for Direct3D 12 and Vulkan respectively depending
# on the target platform.
#
# On Ninja/Makefile generators, all dependent source files (#includes) will also
# trigger shader recompilation. On other generators, dependencies are tracked at
# CMake generation time. Changes to shader include relationships (adding or
# removing #include directives) require reconfiguring the CMake project to
# update the dependency graph. Shader recompilation still works correctly when
# existing dependency files change.
#
# Compiler optimization flags are automatically set depending on the build type.
# For Debug builds, shaders are compiled with no optimizations and include all
# debug information as well as the shader source code. For Release builds of any
# kind, the maximum optimization level is enabled and all debug information is
# stripped from the shader blob. In all cases, no reflection data is included
# unless INCLUDE_REFLECTION is specified.
#
# Parameters:
#
#   OUT_PATHS
#       Name of a variable in the parent scope that will receive the full path
#       to the generated .cso and/or .spv files.
#
#   SOURCE
#       Path to the HLSL shader source file.
#
#   WHENCE
#       Base path to HLSL shader files. Assumes ${CMAKE_CURRENT_SOURCE_DIR} by
#       default if omitted.
#
#   DESTINATION
#       Directory where the compiled .cso/.spv and generated .d files will be
#       written.
#
#   ENTRYPOINT
#       Shader entry point function name (e.g. "PSMain", "VSMain", "CSMain").
#
#   PROFILE
#       Shader profile to compile for (e.g. "ps_6_7", "vs_6_7", "cs_6_5").
#
#   VARIANT (optional)
#       Appends a suffix to the base filename (before the extension) to produce
#       a shader variant. This is useful when building multiple versions of the
#       same shader using different macro sets.
#
#   MACROS (optional)
#       List of preprocessor defines to pass to the shader.
#
#   INCLUDE_REFLECTION (optional)
#       If specified, shaders are compiled with reflection information.
#
# Output:
#   OUT_PATHS is set to a list of full paths of the generated .cso/.spv files.
function(compile_shader)
    # Parse arguments
    set(options
        INCLUDE_REFLECTION
    )
    set(oneValueArgs
        OUT_PATHS
        SOURCE
        WHENCE
        DESTINATION
        ENTRYPOINT
        PROFILE
        VARIANT
    )
    set(multiValueArgs
        MACROS
    )

    cmake_parse_arguments(ARG
        "${options}"
        "${oneValueArgs}"
        "${multiValueArgs}"
        ${ARGN}
    )

    # Extract paths.
    #
    # WHENCE specifies the root path for shaders.
    # SOURCE is the path to the shader relative to the current source directory.
    # Break those down into:
    #   _base_path: path to the file relative to WHENCE, used for the virtual file system structure
    #   _base_name: file name without path or extension so we can turn <file>.hlsl into <file>.cso/spv/d
    if (NOT ARG_WHENCE)
        set(ARG_WHENCE ${CMAKE_CURRENT_SOURCE_DIR})
    endif ()
    get_filename_component(_source_abs_path "${ARG_SOURCE}" ABSOLUTE)
    file(RELATIVE_PATH _source_rel_path "${ARG_WHENCE}" "${_source_abs_path}")
    get_filename_component(_base_path "${_source_rel_path}" DIRECTORY)
    get_filename_component(_base_name "${_source_rel_path}" NAME_WE)
    if (_source_rel_path MATCHES "^\\.\\.")
        # Require files to be relative to the root directory
        message(SEND_ERROR "Cannot add file ${ARG_SOURCE}: File must be in a subdirectory of ${ARG_WHENCE}")
        continue()
    endif ()

    # Apply variant name suffix if specified
    if (ARG_VARIANT_NAME)
        set(_final_name "${_base_name}_${ARG_VARIANT_NAME}")
    else ()
        set(_final_name "${_base_name}")
    endif ()

    set(_dep_file "${ARG_DESTINATION}/${_base_path}/${_final_name}.d")
    set(_out_shader_path "${ARG_DESTINATION}/${_base_path}/${_final_name}")
    set(_out_dxil_path "${_out_shader_path}.cso")
    set(_out_spirv_path "${_out_shader_path}.spv")

    list(TRANSFORM ARG_MACROS PREPEND "-D" OUTPUT_VARIABLE _dxc_macro_args)

    # Setup common DXC flags
    set(_dxc_common_flags
        -T "${ARG_PROFILE}"
        -E "${ARG_ENTRYPOINT}"
        ${_dxc_macro_args}
    )
    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        list(APPEND _dxc_common_flags "-Od")
    else ()
        list(APPEND _dxc_common_flags "-O3")
    endif ()

    # Flags for dependency generation
    set(_dxc_deps_flags ${_dxc_common_flags})
    if (NOT ARG_INCLUDE_REFLECTION)
        list(APPEND _dxc_deps_flags "-MD" "-MF" "${_dep_file}")
    endif ()

    # Flags for DXIL compilation
    if (DXC_DXIL_SUPPORTED)
        set(_dxc_dxil_flags ${_dxc_common_flags})
        if (NOT ARG_INCLUDE_REFLECTION)
            list(APPEND _dxc_dxil_flags "-Qstrip_reflect")
        endif ()
        if (CMAKE_BUILD_TYPE STREQUAL "Debug")
            list(APPEND _dxc_dxil_flags "-Qembed_debug" "-Zi")
        else ()
            list(APPEND _dxc_dxil_flags "-Qstrip_debug")
        endif ()
    endif ()

    # Flags for SPIR-V compilation
    if (DXC_SPIRV_SUPPORTED)
        set(_dxc_spirv_flags ${_dxc_common_flags} "-spirv")
        if (ARG_INCLUDE_REFLECTION)
            list(APPEND _dxc_spirv_flags "-fspv-reflect")
        endif ()
        if (CMAKE_BUILD_TYPE STREQUAL "Debug")
            list(APPEND _dxc_spirv_flags "-fspv-debug=vulkan-with-source")
        endif ()
    endif ()

    # Set up commands and outputs
    set(_depfile_command COMMAND "${DXC_EXECUTABLE}" ${_dxc_deps_flags} "${_source_abs_path}")
    set(_compile_commands "")
    set(_outputs "")
    if (DXC_DXIL_SUPPORTED)
        list(APPEND _compile_commands
            COMMAND "${DXC_EXECUTABLE}"
                ${_dxc_dxil_flags}
                -Fo "${_out_dxil_path}"
                "${_source_abs_path}"
        )
        list(APPEND _outputs "${_out_dxil_path}")
    endif ()
    if (DXC_SPIRV_SUPPORTED)
        list(APPEND _compile_commands
            COMMAND "${DXC_EXECUTABLE}"
                ${_dxc_spirv_flags}
                -Fo "${_out_spirv_path}"
                "${_source_abs_path}"
        )
        list(APPEND _outputs "${_out_spirv_path}")
    endif ()
    if (APPLE) # TODO: DXC_METAL_SUPPORTED instead of APPLE
        # TODO: add additional commands to ${_compile_commands}:
        # - use spirv-tools to convert SPIR-V to Metal
        # - use metal to compile the shader
        # - use metallib to package shader
        # TODO: add .metallib file to _outputs
    endif ()


    # Add custom commands
    if (CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
        # Make use of DEPFILE to automatically rebuild dependency graphs when
        # includes are changed in HLSL source files.

        # Compile shader
        add_custom_command(
            OUTPUT ${_outputs}

            # Generate dependency file
            ${_depfile_command}

            # Compile shader
            ${_compile_commands}

            DEPFILE "${_dep_file}"
            COMMENT "Compiling ${_source_abs_path} to ${_outputs}"
        )
    else()
        # Fallback for generators without DEPFILE support
        message(WARNING
            "compile_hlsl_shader: This generator does not support depfiles. "
            "Shader include dependency changes (adding/removing #include directives) "
            "will not be detected automatically. You must reconfigure the project "
            "after modifying include relationships."
        )

        # Generate dependency file
        execute_process(
            ${_depfile_command}
            RESULT_VARIABLE _result
            OUTPUT_QUIET
            ERROR_QUIET
        )

        if (NOT _result EQUAL 0)
            message(FATAL_ERROR "DXC exited with error code ${_result} when generating dependencies for ${_source_abs_path}")
        endif ()

        # Parse dependency file
        file(READ "${_dep_file}" _depfile_contents)
        string(REGEX REPLACE "^[^:]*:" "" _deps_raw "${_depfile_contents}")
        string(REGEX REPLACE "[ \\\n]+" ";" _deps_raw "${_deps_raw}")
        list(FILTER _deps_raw EXCLUDE REGEX "^$")

        # Normalize paths
        set(_deps "")
        foreach(_dep ${_deps_raw})
            get_filename_component(_abs_dep "${_dep}" ABSOLUTE)
            list(APPEND _deps "${_abs_dep}")
        endforeach()

        # Compile shader
        add_custom_command(
            OUTPUT ${_outputs}
            ${_compile_commands}
            DEPENDS ${_deps}
            COMMENT "Compiling ${_source_abs_path} to ${_outputs}"
        )
    endif()

    set(${ARG_OUT_PATHS} "${_outputs}" PARENT_SCOPE)
endfunction()
