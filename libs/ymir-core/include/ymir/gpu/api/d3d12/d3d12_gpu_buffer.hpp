#pragma once

#include <ymir/gpu/api/base/gpu_buffer.hpp>

#include <ymir/gpu/api/d3d12/wrappers/d3d12_device.hpp>
#include <ymir/gpu/api/d3d12/wrappers/d3d12_resource.hpp>

#include <ymir/gpu/common/gpu_result.hpp>

namespace ymir::gpu {

class D3D12Buffer : public IGPUBuffer {
public:
    static constexpr Backend kBackend = Backend::Direct3D12;

    D3D12Buffer(d3d12::D3D12Device &device, const BufferSpec &spec, d3d12::D3D12Resource resource);

    static GPUObjectResult<IGPUBuffer> Create(d3d12::D3D12Device &device, const BufferSpec &spec);

    void SetName(std::string_view name) override;

    void *Map(uint64 start = 0u, uint64 end = 0u) override;
    void Unmap(uint64 start = 0u, uint64 end = 0u) override;
    uint64 GetAddress() override;

    d3d12::D3D12Resource &GetResource() {
        return m_resource;
    }

    const d3d12::D3D12Resource &GetResource() const {
        return m_resource;
    }

private:
    d3d12::D3D12Device &m_device;
    d3d12::D3D12Resource m_resource;

    D3D12_RANGE MakeRange(uint64 start, uint64 end);
};

} // namespace ymir::gpu
