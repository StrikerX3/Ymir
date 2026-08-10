#pragma once

#include <string>
#include <vector>

namespace ymir::gpu {

enum class ShaderLanguage {
    None, // Precompiled shader blobs
    HLSL, // D3D12, Vulkan
    MSL,  // Metal
};

enum class ShaderBytecodeFormat {
    None,
    DXIL,     // D3D12
    SPIRV,    // Vulkan
    MetalLib, // Metal
};

enum class ShaderStage {
    // Graphics pipeline
    Vertex,
    // Hull,
    // Domain,
    // Geometry,
    Pixel,

    // Compute pipeline
    Compute,

    // // Mesh pipeline
    // Amplification,
    // Mesh,

    // // Ray tracing
    // RTRayGeneration,
    // RTIntersection,
    // RTAnyHit,
    // RTClosestHit,
    // RTMiss,
    // RTCallable,
};

template <ShaderStage stage>
struct CompiledShader {
    static constexpr ShaderStage kStage = stage;

    ShaderLanguage language = ShaderLanguage::None;
    ShaderBytecodeFormat format = ShaderBytecodeFormat::None;
    std::vector<char> bytecode;
    std::string entrypoint;
};

using VertexShader = CompiledShader<ShaderStage::Vertex>;
using PixelShader = CompiledShader<ShaderStage::Pixel>;
using ComputeShader = CompiledShader<ShaderStage::Compute>;

/// @brief Shader macro specification.
struct ShaderMacro {
    std::string name;
    std::string value;
};

/// @brief Specifications for compiling shaders from source code.
/// You must specify a valid language and bytecode type combination:
/// - HLSL -> DXIL or SPIRV
/// - MSL -> MetalLib
template <ShaderStage stage>
struct ShaderCompileSpec {
    static constexpr ShaderStage kStage = stage;

    ShaderLanguage language = ShaderLanguage::None;
    ShaderBytecodeFormat format = ShaderBytecodeFormat::None;
    std::string name;
    std::string sourceCode;
    std::string entrypoint;
    std::vector<ShaderMacro> macros;
    bool debug = false;
    bool optimize = true;
};

using VertexShaderCompileSpec = ShaderCompileSpec<ShaderStage::Vertex>;
using PixelShaderCompileSpec = ShaderCompileSpec<ShaderStage::Pixel>;
using ComputeShaderCompileSpec = ShaderCompileSpec<ShaderStage::Compute>;

} // namespace ymir::gpu
