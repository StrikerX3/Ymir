#pragma once

#include <ymir/gpu/api/base/gpu_surface.hpp>

#include <ymir/gpu/api/base/gpu_command_queue.hpp>

#include <ymir/gpu/api/d3d12/wrappers/d3d12_device.hpp>
#include <ymir/gpu/api/d3d12/wrappers/d3d12_swap_chain.hpp>

#include <ymir/gpu/common/gpu_window_params.hpp>

#include <ymir/core/types.hpp>

namespace ymir::gpu {

class D3D12Surface : public IGPUSurface {
public:
    static constexpr Backend kBackend = Backend::Direct3D12;

    D3D12Surface(d3d12::D3D12Device &device, d3d12::D3D12SwapChain swapchain);

    static GPUObjectResult<IGPUSurface> Create(d3d12::D3D12Device &device, const WindowParams &windowParams,
                                               const IGPUCommandQueue &queue, uint32 maxFramesInFlight, bool debug,
                                               const util::PropertyBag *props);

    void SetName(std::string_view name) override;

    d3d12::D3D12SwapChain &GetSwapChain() {
        return m_swapchain;
    }

    const d3d12::D3D12SwapChain &GetSwapChain() const {
        return m_swapchain;
    }

private:
    d3d12::D3D12Device &m_device;
    d3d12::D3D12SwapChain m_swapchain;
};

} // namespace ymir::gpu
