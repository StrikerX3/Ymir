#include <ymir/gpu/gpu_shaders.hpp>

// TODO: consider using shaderc

namespace ymir::gpu {

GPUValueResult<CompiledShader> CompileShader(const ShaderCompileSpec &spec) {
    if (spec.language != ShaderLanguage::HLSL) {
        return GPUOperationError{"Unsupported shader language provided to compiler"};
    }
    if (spec.format != ShaderBytecodeFormat::SPIRV) {
        return GPUOperationError{"Unsupported shader bytecode format provided to compiler"};
    }

    // TODO: configure and invoke compiler, parse result, return appropriate response

    return GPUOperationError{"Shader compilation is unimplemented"};
}

std::optional<GPUOperationError> ValidateShader(const CompiledShader &spec) {
    if (spec.format != ShaderBytecodeFormat::SPIRV) {
        return GPUOperationError{"Unsupported shader bytecode format provided to compiler"};
    }

    // TODO: validate bytecode

    return std::nullopt;
}

} // namespace ymir::gpu
