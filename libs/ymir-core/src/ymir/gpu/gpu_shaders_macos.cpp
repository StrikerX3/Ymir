#include <ymir/gpu/gpu_shaders.hpp>

namespace ymir::gpu {

GPUValueResult<CompiledShader> CompileShader(const ShaderCompileSpec &spec) {
    if (spec.language != ShaderLanguage::MSL) {
        return GPUOperationError{"Unsupported shader language provided to Metal compiler"};
    }
    if (spec.format != ShaderBytecodeFormat::MetalLib) {
        return GPUOperationError{"Unsupported shader bytecode format provided to Metal compiler"};
    }

    // TODO: configure and invoke compiler, parse result, return appropriate response

    return GPUOperationError{"Metal shader compilation is unimplemented"};
}

std::optional<GPUOperationError> ValidateShader(const CompiledShader &spec) {
    if (spec.format != ShaderBytecodeFormat::MetalLib) {
        return GPUOperationError{"Unsupported shader bytecode format provided to Metal compiler"};
    }

    // TODO: validate bytecode

    return std::nullopt;
}

} // namespace ymir::gpu
