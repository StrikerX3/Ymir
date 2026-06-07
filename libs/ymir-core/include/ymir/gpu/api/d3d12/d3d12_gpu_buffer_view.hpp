#pragma once

#include <ymir/gpu/api/base/gpu_buffer_view.hpp>

#include <ymir/gpu/api/d3d12/d3d12_descriptor_manager.hpp>

#include <ymir/gpu/common/gpu_result.hpp>

namespace ymir::gpu {

class D3D12BufferView : public IGPUBufferView {
public:
    static constexpr Backend kBackend = Backend::Direct3D12;

    D3D12BufferView(D3D12DescriptorManager &descMgr, const BufferViewSpec &spec, D3D12DescriptorAllocation alloc);
    ~D3D12BufferView();

    static GPUObjectResult<IGPUBufferView> Create(D3D12DescriptorManager &descMgr, const BufferViewSpec &spec);

    void SetName(std::string_view name) override;

private:
    D3D12DescriptorManager &m_descMgr;
    D3D12DescriptorAllocation m_alloc;
};

} // namespace ymir::gpu
