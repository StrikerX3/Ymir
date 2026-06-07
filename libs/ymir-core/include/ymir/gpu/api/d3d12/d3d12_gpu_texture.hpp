#pragma once

#include <ymir/gpu/api/base/gpu_texture.hpp>

#include <ymir/gpu/api/d3d12/wrappers/d3d12_device.hpp>
#include <ymir/gpu/api/d3d12/wrappers/d3d12_resource.hpp>

#include <ymir/gpu/common/gpu_result.hpp>

namespace ymir::gpu {

class D3D12Texture : public IGPUTexture {
public:
    static constexpr Backend kBackend = Backend::Direct3D12;

    D3D12Texture(d3d12::D3D12Device &device, const TextureSpec &spec, d3d12::D3D12Resource resource);

    static GPUObjectResult<IGPUTexture> Create(d3d12::D3D12Device &device, const TextureSpec &spec);

    void SetName(std::string_view name) override;

    d3d12::D3D12Resource &GetResource() {
        return m_resource;
    }

    const d3d12::D3D12Resource &GetResource() const {
        return m_resource;
    }

private:
    d3d12::D3D12Device &m_device;
    d3d12::D3D12Resource m_resource;
};

} // namespace ymir::gpu
