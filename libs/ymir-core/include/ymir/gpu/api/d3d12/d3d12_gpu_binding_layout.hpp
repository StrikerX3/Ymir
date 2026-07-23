#pragma once

#include <ymir/gpu/api/base/gpu_binding_layout.hpp>

#include <ymir/gpu/common/gpu_result.hpp>

#include <ymir/gpu/api/d3d12/wrappers/d3d12_device.hpp>

namespace ymir::gpu {

class D3D12BindingLayout : public IGPUBindingLayout {
public:
    static constexpr Backend kBackend = Backend::Direct3D12;

    D3D12BindingLayout();

    static GPUObjectResult<IGPUBindingLayout> Create(d3d12::D3D12Device &device, const ManualBindingLayoutSpec &spec);
    static GPUObjectResult<IGPUBindingLayout> Create(d3d12::D3D12Device &device,
                                                     const ReflectionBindingLayoutSpec &spec);

private:
};

} // namespace ymir::gpu
