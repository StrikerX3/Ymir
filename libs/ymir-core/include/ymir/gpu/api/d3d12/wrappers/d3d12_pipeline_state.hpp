#pragma once

/**
@file
@brief Defines `D3D12PipelineState`, a wrapper for `ID3D12PipelineState` objects.
*/

#include "d3d12_device.hpp"
#include "d3d12_object_wrapper.hpp"
#include "d3d12_root_signature.hpp"

#include <d3d12.h>

// TODO: graphics PSO -- device->CreateGraphicsPipelineState()
// TODO: generic PSO -- device->CreatePipelineState()

namespace ymir::gpu::d3d12 {

/// @brief Manages an `ID3D12PipelineState`.
class D3D12PipelineState final : public D3D12ObjectWrapper<ID3D12PipelineState> {
public:
    class ComputePSBuilder;

    /// @brief Creates a compute pipeline state object builder bound to this instance.
    /// @return a compute pipeline state object builder
    ComputePSBuilder ComputeBuilder();

    /// @brief Creates a compute shader pipeline state object.
    /// @param[in] device the device instance that will own the pipeline state object
    /// @param[in] rootSig the root signature
    /// @param[in] shaderBytecode pointer to the shader bytecode blob
    /// @param[in] bytecodeLength size (in bytes) of the shader bytecode blob
    /// @param[in] flags pipeline state object flags. Defaults to `D3D12_PIPELINE_STATE_FLAG_NONE`
    /// @param[in] nodeMask the node mask. Defaults to 0
    /// @param[in] cachedPSOBlob pointer to the cached pipeline state object blob. Defaults to `nullptr`
    /// @param[in] cachedPSOBlobSize size of the cached pipeline state object blob. Defaults to 0
    /// @return the result of the attempt to create the pipeline state object
    HRESULT CreateCompute(const D3D12Device &device, const D3D12RootSignature &rootSig, const void *shaderBytecode,
                          size_t bytecodeLength, D3D12_PIPELINE_STATE_FLAGS flags = D3D12_PIPELINE_STATE_FLAG_NONE,
                          UINT nodeMask = 0u, const void *cachedPSOBlob = nullptr, size_t cachedPSOBlobSize = 0) {
        const D3D12_COMPUTE_PIPELINE_STATE_DESC desc = {
            .pRootSignature = rootSig.GetPointer(),
            .CS = {.pShaderBytecode = shaderBytecode, .BytecodeLength = bytecodeLength},
            .NodeMask = 0,
            .CachedPSO = {.pCachedBlob = cachedPSOBlob, .CachedBlobSizeInBytes = cachedPSOBlobSize},
            .Flags = flags,
        };
        return device->CreateComputePipelineState(&desc, IID_PPV_ARGS(m_object.put()));
    }
};

// -----------------------------------------------------------------------------

class D3D12PipelineState::ComputePSBuilder {
    ComputePSBuilder(D3D12PipelineState &parent)
        : m_parent(parent) {}

    friend class D3D12PipelineState;

public:
    /// @brief Sets the flags for the pipeline state object. The default value is `D3D12_PIPELINE_STATE_FLAG_NONE`.
    /// @param[in] flags the flags
    /// @return this builder
    ComputePSBuilder &Flags(D3D12_PIPELINE_STATE_FLAGS flags) {
        m_flags = flags;
        return *this;
    }

    /// @brief Sets the node mask for the pipeline state object. The default value is 0.
    /// @param[in] nodeMask the node mask
    /// @return this builder
    ComputePSBuilder &NodeMask(UINT nodeMask) {
        m_nodeMask = nodeMask;
        return *this;
    }

    /// @brief Sets the cached PSO blob for the pipeline state object. The default value is empty.
    /// @param[in] blob pointer to the cached PSO blob data
    /// @param[in] length size of the cached PSO blob data
    /// @return this builder
    ComputePSBuilder &CachedPSO(const void *blob, size_t length) {
        m_cachedPSOBlob = blob;
        m_cachedPSOBlobSize = length;
        return *this;
    }

    HRESULT Build(const D3D12Device &device, const D3D12RootSignature &rootSig, const void *shaderBytecode,
                  size_t bytecodeLength) {
        return m_parent.CreateCompute(device, rootSig, shaderBytecode, bytecodeLength, m_flags, m_nodeMask,
                                      m_cachedPSOBlob, m_cachedPSOBlobSize);
    }

private:
    D3D12PipelineState &m_parent;

    D3D12_PIPELINE_STATE_FLAGS m_flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    UINT m_nodeMask = 0u;
    const void *m_cachedPSOBlob = nullptr;
    size_t m_cachedPSOBlobSize = 0;
};

inline auto D3D12PipelineState::ComputeBuilder() -> ComputePSBuilder {
    return ComputePSBuilder{*this};
}

} // namespace ymir::gpu::d3d12
