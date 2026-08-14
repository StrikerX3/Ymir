#pragma once

/**
@file
@brief Defines `D3D12PipelineState`, a wrapper for `ID3D12PipelineState` objects.
*/

#include "d3d12_device.hpp"
#include "d3d12_object_wrapper.hpp"

#include <d3d12.h>

// TODO: generic PSO -- device->CreatePipelineState()

namespace ymir::gpu::d3d12 {

/// @brief Manages an `ID3D12PipelineState`.
class D3D12PipelineState final : public D3D12ObjectWrapper<ID3D12PipelineState> {
public:
    D3D12PipelineState() = default;
    D3D12PipelineState(wil::com_ptr_nothrow<ID3D12PipelineState> &&ptr)
        : D3D12ObjectWrapper(std::move(ptr)) {}
    D3D12PipelineState(ID3D12PipelineState *ptr)
        : D3D12ObjectWrapper(ptr) {}

    /// @brief Creates a graphics pipeline state object.
    /// @param[in] desc the graphics pipeline state descriptor
    /// @return the result of the attempt to create the graphics pipeline state object
    HRESULT CreateGraphics(const D3D12Device &device, const D3D12_GRAPHICS_PIPELINE_STATE_DESC &desc) {
        return device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(m_object.put()));
    }

    /// @brief Creates a compute shader pipeline state object.
    /// @param[in] desc the compute pipeline state descriptor
    /// @return the result of the attempt to create the pipeline state object
    HRESULT CreateCompute(const D3D12Device &device, const D3D12_COMPUTE_PIPELINE_STATE_DESC &desc) {
        return device->CreateComputePipelineState(&desc, IID_PPV_ARGS(m_object.put()));
    }
};

} // namespace ymir::gpu::d3d12
