#include <ymir/gpu/api/d3d12/d3d12_gpu_binding_layout.hpp>

namespace ymir::gpu {

D3D12BindingLayout::D3D12BindingLayout()
    : IGPUBindingLayout(kBackend) {}

GPUObjectResult<IGPUBindingLayout> D3D12BindingLayout::Create(d3d12::D3D12Device &device,
                                                              const ManualBindingLayoutSpec &spec) {
    return GPUOperationError{"Unimplemented"};
}

GPUObjectResult<IGPUBindingLayout> D3D12BindingLayout::Create(d3d12::D3D12Device &device,
                                                              const ReflectionBindingLayoutSpec &spec) {
    return GPUOperationError{"Unimplemented"};
}

} // namespace ymir::gpu
