#include <ymir/gpu/shaders/gpu_shaders.hpp>

namespace ymir::gpu {

template <ShaderStage stage>
util::ValueResult<CompiledShader<stage>> DoCompileShader(const ShaderCompileSpec<stage> &spec) {
    if (spec.language != ShaderLanguage::MSL) {
        return util::ErrorMessage{"Unsupported shader language provided to Metal compiler"};
    }
    if (spec.format != ShaderBytecodeFormat::MetalLib) {
        return util::ErrorMessage{"Unsupported shader bytecode format provided to Metal compiler"};
    }

    // TODO: configure and invoke compiler, parse result, return appropriate response

    return util::ErrorMessage{"Metal shader compilation is unimplemented"};
}

template <ShaderStage stage>
util::VoidResult<> DoValidateShader(CompiledShader<stage> &spec) {
    if (spec.format != ShaderBytecodeFormat::MetalLib) {
        return util::ErrorMessage{"Unsupported shader bytecode format provided to Metal compiler"};
    }

    // TODO: validate bytecode

    return {};
}

// ---------------------------------------------------------------------------------------------------------------------

util::ValueResult<VertexShader> CompileShader(const VertexShaderCompileSpec &spec) {
    return DoCompileShader(spec);
}

util::ValueResult<PixelShader> CompileShader(const PixelShaderCompileSpec &spec) {
    return DoCompileShader(spec);
}

util::ValueResult<ComputeShader> CompileShader(const ComputeShaderCompileSpec &spec) {
    return DoCompileShader(spec);
}

// -----------------------------------------------------------------------------

util::VoidResult<> ValidateShader(VertexShader &shader) {
    return DoValidateShader(shader);
}

util::VoidResult<> ValidateShader(PixelShader &shader) {
    return DoValidateShader(shader);
}

util::VoidResult<> ValidateShader(ComputeShader &shader) {
    return DoValidateShader(shader);
}

} // namespace ymir::gpu
