#pragma once

#include <ymir/gpu/api/base/gpu_command_list.hpp>

#include <ymir/gpu/api/d3d12/wrappers/d3d12_commands.hpp>
#include <ymir/gpu/api/d3d12/wrappers/d3d12_device.hpp>

namespace ymir::gpu {

class D3D12CommandQueue;

class D3D12CommandList final : public IGPUCommandList {
public:
    static constexpr Backend kBackend = Backend::Direct3D12;

    D3D12CommandList(d3d12::D3D12Device &parentDevice, D3D12CommandQueue &parentQueue,
                     d3d12::D3D12CommandAllocator &&allocator, d3d12::D3D12GraphicsCommandList &&list);

    void SetName(std::string_view name) override;

    void Reset() override;
    void Begin() override;
    void End() override;

    d3d12::D3D12CommandAllocator &GetAllocator() {
        return m_allocator;
    }

    const d3d12::D3D12CommandAllocator &GetAllocator() const {
        return m_allocator;
    }

    d3d12::D3D12GraphicsCommandList GetCommandList() {
        return m_list;
    }

    const d3d12::D3D12GraphicsCommandList GetCommandList() const {
        return m_list;
    }

private:
    d3d12::D3D12Device &m_parentDevice;
    D3D12CommandQueue &m_parentQueue;
    d3d12::D3D12CommandAllocator m_allocator;
    d3d12::D3D12GraphicsCommandList m_list;
};

} // namespace ymir::gpu
