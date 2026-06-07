#pragma once

/**
@file
@brief GPU shader operations.

Shaders can be loaded in Ymir in source code or precompiled forms. Ymir uses the following compilers:
- DXC on Windows
- shaderc on Linux and FreeBSD
- Metal shader compiler on macOS

`ymir::gpu::CompileShader(const ShaderCompilerSpec &spec)` is used to compile a shader from source code. Ymir will
attempt to pick the best available compiler automatic from the given combination of language and bytecode type:
- HLSL -> DXIL   : DXC or shaderc
- HLSL -> SPIR-V : DXC or shaderc
- MSL  -> MetaLib: Metal shader compiler

Direct3D 12 uses DXIL shaders.
Vulkan uses SPIR-V shaders.
Metal uses MetaLib shaders.
*/

#include <ymir/gpu/common/gpu_defs.hpp>
#include <ymir/gpu/common/gpu_result.hpp>

#include <optional>

namespace ymir::gpu {

/// @brief Compiles the specified shader.
/// @param[in] spec shader compilation specifications
/// @return the compiled shader or an error
GPUValueResult<CompiledShader> CompileShader(const ShaderCompileSpec &spec);

/// @brief Validates the specified shader bytecode.
/// @param[in] spec the shader bytecode specifications
/// @return an error if validation fails
std::optional<GPUOperationError> ValidateShader(const CompiledShader &spec);

} // namespace ymir::gpu
