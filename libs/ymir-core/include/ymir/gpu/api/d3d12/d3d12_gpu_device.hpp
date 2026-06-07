#pragma once

#include <ymir/gpu/api/base/gpu_device.hpp>

#include <ymir/gpu/api/d3d12/d3d12_descriptor_manager.hpp>
#include <ymir/gpu/api/d3d12/d3d12_params.hpp>

#include "wrappers/d3d12_device.hpp"

namespace ymir::gpu {

class D3D12GPUDevice final : public IGPUDevice {
public:
    static constexpr Backend kBackend = Backend::Direct3D12;

    static GPUObjectResult<IGPUDevice> Create(const D3D12GPUDeviceSpec &spec);

    D3D12GPUDevice(std::unique_ptr<d3d12::D3D12Device> device, std::unique_ptr<D3D12DescriptorManager> &&descMgr,
                   bool debug)
        : IGPUDevice(kBackend)
        , m_device(std::move(device))
        , m_descMgr(std::move(descMgr))
        , m_debug(debug) {}

    void SetName(std::string_view name) override;

    GPUObjectResult<IGPUCommandQueue> CreateCommandQueue(CommandQueueType type,
                                                         const util::PropertyBag *props = nullptr) override;

    GPUObjectResult<IGPUSurface> CreateSurface(const WindowParams &windowParams, const IGPUCommandQueue &queue,
                                               uint32 maxFramesInFlight,
                                               const util::PropertyBag *props = nullptr) override;

    GPUObjectResult<IGPUTexture> CreateTexture(const TextureSpec &spec) override;
    GPUObjectResult<IGPUTextureView> CreateTextureView(const TextureViewSpec &spec) override;

    GPUObjectResult<IGPUBuffer> CreateBuffer(const BufferSpec &spec) override;
    GPUObjectResult<IGPUBufferView> CreateBufferView(const BufferViewSpec &spec) override;

    // -----------------------------------------------------------------------------------------------------------------
    // Native object accessors

    d3d12::D3D12Device &GetD3D12Device() {
        return *m_device;
    }

    const d3d12::D3D12Device &GetD3D12Device() const {
        return *m_device;
    }

private:
    std::unique_ptr<d3d12::D3D12Device> m_device;
    std::unique_ptr<D3D12DescriptorManager> m_descMgr;
    bool m_debug;
};

} // namespace ymir::gpu
