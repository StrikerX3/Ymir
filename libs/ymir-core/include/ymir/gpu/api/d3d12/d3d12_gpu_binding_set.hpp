#pragma once

#include <ymir/gpu/api/base/gpu_binding_set.hpp>

namespace ymir::gpu {

class D3D12GPUBindingSet : public IGPUBindingSet {
public:
    static constexpr Backend kBackend = Backend::Direct3D12;

    D3D12GPUBindingSet();

private:
};

} // namespace ymir::gpu
