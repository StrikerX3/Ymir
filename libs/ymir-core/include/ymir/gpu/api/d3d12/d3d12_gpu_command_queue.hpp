#pragma once

#include <ymir/gpu/api/base/gpu_command_queue.hpp>

#include <ymir/gpu/api/d3d12/wrappers/d3d12_commands.hpp>
#include <ymir/gpu/api/d3d12/wrappers/d3d12_fence.hpp>

#include <ymir/gpu/api/d3d12/d3d12_params.hpp>

namespace ymir::gpu {

class D3D12CommandQueue final : public IGPUCommandQueue {
public:
    static constexpr Backend kBackend = Backend::Direct3D12;

    D3D12CommandQueue(d3d12::D3D12Device &device, d3d12::D3D12CommandQueue &&queue, d3d12::D3D12Fence &&fence,
                      CommandQueueType type);

    void SetName(std::string_view name) override;

    static GPUObjectResult<IGPUCommandQueue> Create(d3d12::D3D12Device &device, CommandQueueType type,
                                                    const util::PropertyBag *props);

    GPUObjectResult<IGPUCommandList> CreateCommandList(const util::PropertyBag *props = nullptr) override;
    void CommitCommandList(const IGPUCommandList &list) override;
    void Wait() override;

    d3d12::D3D12CommandQueue &GetCommandQueue() {
        return m_queue;
    }

    const d3d12::D3D12CommandQueue &GetCommandQueue() const {
        return m_queue;
    }

private:
    d3d12::D3D12Device &m_device;
    d3d12::D3D12CommandQueue m_queue;
    d3d12::D3D12Fence m_fence;
    D3D12_COMMAND_LIST_TYPE m_d3d12Type;
};

} // namespace ymir::gpu
