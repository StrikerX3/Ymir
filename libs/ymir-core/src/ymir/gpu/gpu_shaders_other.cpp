#include <ymir/gpu/gpu_shaders.hpp>

namespace ymir::gpu {

GPUValueResult<CompiledShader> CompileShader(const ShaderCompileSpec &spec) {
    return GPUOperationError{"Unsupported platform"};
}

std::optional<GPUOperationError> ValidateShader(const CompiledShader &spec) {
    return GPUOperationError{"Unsupported platform"};
}

} // namespace ymir::gpu
