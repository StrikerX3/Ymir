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

    if (spec.bytecode.size() < 4) {
        return util::ErrorMessage{"MetalLib bytecode is too small or empty"};
    }

    if (spec.bytecode[0] != 'M' || spec.bytecode[1] != 'T' || spec.bytecode[2] != 'L' || spec.bytecode[3] != 'B') {
        return util::ErrorMessage{"Invalid MetalLib magic header"};
    }

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
