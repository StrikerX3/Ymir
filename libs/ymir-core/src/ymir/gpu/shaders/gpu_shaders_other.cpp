#include <ymir/gpu/shaders/gpu_shaders.hpp>

namespace ymir::gpu {

util::ValueResult<VertexShader> CompileShader(const VertexShaderCompileSpec &spec) {
    return util::ErrorMessage{"Unsupported platform"};
}

util::ValueResult<PixelShader> CompileShader(const PixelShaderCompileSpec &spec) {
    return util::ErrorMessage{"Unsupported platform"};
}

util::ValueResult<ComputeShader> CompileShader(const ComputeShaderCompileSpec &spec) {
    return util::ErrorMessage{"Unsupported platform"};
}

// -----------------------------------------------------------------------------

util::VoidResult<> ValidateShader(VertexShader &shader) {
    return util::ErrorMessage{"Unsupported platform"};
}

util::VoidResult<> ValidateShader(PixelShader &shader) {
    return util::ErrorMessage{"Unsupported platform"};
}

util::VoidResult<> ValidateShader(ComputeShader &shader) {
    return util::ErrorMessage{"Unsupported platform"};
}

} // namespace ymir::gpu
