#include <ymir/gpu/api/d3d12/d3d12_gpu_binding_set.hpp>

namespace ymir::gpu {

D3D12GPUBindingSet::D3D12GPUBindingSet()
    : IGPUBindingSet(kBackend) {}

} // namespace ymir::gpu
