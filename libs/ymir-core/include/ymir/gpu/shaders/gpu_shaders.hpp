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
- HLSL -> DXIL    : DXC
- HLSL -> SPIR-V  : DXC or shaderc
- MSL  -> MetalLib: Metal shader compiler

Direct3D 12 uses DXIL shaders.
Vulkan uses SPIR-V shaders.
Metal uses MetalLib shaders.
*/

#include "gpu_shader_types.hpp"

#include <ymir/util/result.hpp>

namespace ymir::gpu {

/// @brief Compiles the specified vertex shader.
/// @param[in] spec vertex shader compilation specifications
/// @return the compiled vertex shader or an error
util::ValueResult<VertexShader> CompileShader(const VertexShaderCompileSpec &spec);

/// @brief Compiles the specified pixel shader.
/// @param[in] spec pixel shader compilation specifications
/// @return the compiled pixel shader or an error
util::ValueResult<PixelShader> CompileShader(const PixelShaderCompileSpec &spec);

/// @brief Compiles the specified compute shader.
/// @param[in] spec compute shader compilation specifications
/// @return the compiled compute shader or an error
util::ValueResult<ComputeShader> CompileShader(const ComputeShaderCompileSpec &spec);

// -----------------------------------------------------------------------------

/// @brief Validates the specified vertex shader bytecode.
/// @param[in,out] shader the vertex shader to validate; the bytecode may be modified after validation
/// @return an error if validation fails
util::VoidResult<> ValidateShader(VertexShader &shader);

/// @brief Validates the specified pixel shader bytecode.
/// @param[in,out] shader the pixel shader to validate; the bytecode may be modified after validation
/// @return an error if validation fails
util::VoidResult<> ValidateShader(PixelShader &shader);

/// @brief Validates the specified compute shader bytecode.
/// @param[in,out] shader the compute shader to validate; the bytecode may be modified after validation
/// @return an error if validation fails
util::VoidResult<> ValidateShader(ComputeShader &shader);

} // namespace ymir::gpu
