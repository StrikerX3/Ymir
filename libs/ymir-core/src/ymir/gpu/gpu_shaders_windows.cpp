#include <ymir/gpu/gpu_shaders.hpp>

namespace ymir::gpu {

GPUValueResult<CompiledShader> CompileShader(const ShaderCompileSpec &spec) {
    if (spec.language != ShaderLanguage::HLSL) {
        return GPUOperationError{"Unsupported shader language provided to DXC compiler"};
    }
    if (spec.format != ShaderBytecodeFormat::DXIL && spec.format != ShaderBytecodeFormat::SPIRV) {
        return GPUOperationError{"Unsupported shader bytecode format provided to DXC compiler"};
    }

    // TODO: configure and invoke compiler, parse result, return appropriate response

    return GPUOperationError{"DXC shader compilation is unimplemented"};
}

std::optional<GPUOperationError> ValidateShader(const CompiledShader &spec) {
    if (spec.format != ShaderBytecodeFormat::DXIL && spec.format != ShaderBytecodeFormat::SPIRV) {
        return GPUOperationError{"Unsupported shader bytecode format provided to DXC compiler"};
    }

    // TODO: validate bytecode

    return std::nullopt;
}

} // namespace ymir::gpu
