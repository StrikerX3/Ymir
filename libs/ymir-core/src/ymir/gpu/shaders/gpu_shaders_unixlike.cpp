#include <ymir/gpu/shaders/gpu_shaders.hpp>

// TODO: use shaderc

namespace ymir::gpu {

template <ShaderStage stage>
util::ValueResult<CompiledShader<stage>> DoCompileShader(const ShaderCompileSpec<stage> &spec) {
    if (spec.language != ShaderLanguage::HLSL) {
        return util::ErrorMessage{"Unsupported shader language provided to compiler"};
    }
    if (spec.format != ShaderBytecodeFormat::SPIRV) {
        return util::ErrorMessage{"Unsupported shader bytecode format provided to compiler"};
    }

    // TODO: configure and invoke compiler, parse result, return appropriate response

    return util::ErrorMessage{"Shader compilation is unimplemented"};
}

template <ShaderStage stage>
util::VoidResult<> DoValidateShader(CompiledShader<stage> &spec) {
    if (spec.format != ShaderBytecodeFormat::SPIRV) {
        return util::ErrorMessage{"Unsupported shader bytecode format provided to compiler"};
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
