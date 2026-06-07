#include <ymir/gpu/api/d3d12/d3d12_gpu_device.hpp>

#include <ymir/gpu/api/d3d12/d3d12_descriptor_manager.hpp>
#include <ymir/gpu/api/d3d12/d3d12_gpu_buffer.hpp>
#include <ymir/gpu/api/d3d12/d3d12_gpu_buffer_view.hpp>
#include <ymir/gpu/api/d3d12/d3d12_gpu_command_queue.hpp>
#include <ymir/gpu/api/d3d12/d3d12_gpu_surface.hpp>
#include <ymir/gpu/api/d3d12/d3d12_gpu_texture.hpp>
#include <ymir/gpu/api/d3d12/d3d12_gpu_texture_view.hpp>

#include <ymir/gpu/api/d3d12/helpers/d3d12_debug.hpp>

#include <ymir/gpu/api/d3d12/d3d12_utils.hpp>

#include <fmt/format.h>

namespace ymir::gpu {

GPUObjectResult<IGPUDevice> D3D12GPUDevice::Create(const D3D12GPUDeviceSpec &spec) {
    auto device = std::make_unique<d3d12::D3D12Device>();
    const HRESULT hr = device->Create(spec.adapter, spec.featureLevel);
    if (FAILED(hr)) {
        // TODO: get human-readable error message from HRESULT
        // TODO: get debug messages if available
        return GPUOperationError{fmt::format("Failed to create Direct3D 12 device: error code {:X}", hr)};
    }
    if (spec.debug) {
        ymir::gpu::d3d12::DebugLayer::Get().BreakOnWarnings(device->GetPointer(), true);
    }

    auto descMgrResult = D3D12DescriptorManager::Create(*device, spec.heaps.maxResources, spec.heaps.maxSamplers,
                                                        spec.heaps.maxRTVs, spec.heaps.maxDSVs);
    if (!descMgrResult) {
        return GPUOperationError{
            fmt::format("Failed to create Direct3D 12 device heaps: {}", descMgrResult.Error().message)};
    }

    auto &descMgr = *descMgrResult.Value();
    if (!spec.heaps.resourceHeapName.empty()) {
        descMgr.SetResourceHeapName(spec.heaps.resourceHeapName);
    }
    if (!spec.heaps.samplerHeapName.empty()) {
        descMgr.SetSamplerHeapName(spec.heaps.samplerHeapName);
    }
    if (!spec.heaps.rtvHeapName.empty()) {
        descMgr.SetRTVHeapName(spec.heaps.rtvHeapName);
    }
    if (!spec.heaps.dsvHeapName.empty()) {
        descMgr.SetDSVHeapName(spec.heaps.dsvHeapName);
    }

    return {std::make_unique<D3D12GPUDevice>(std::move(device), std::move(descMgrResult.Value()), spec.debug)};
}

void D3D12GPUDevice::SetName(std::string_view name) {
    (*m_device)->SetName(util::StringToWString(name).c_str());
}

GPUObjectResult<IGPUCommandQueue> D3D12GPUDevice::CreateCommandQueue(CommandQueueType type,
                                                                     const util::PropertyBag *props) {
    return D3D12CommandQueue::Create(*m_device, type, props);
}

GPUObjectResult<IGPUSurface> D3D12GPUDevice::CreateSurface(const WindowParams &windowParams,
                                                           const IGPUCommandQueue &queue, uint32 maxFramesInFlight,
                                                           const util::PropertyBag *props) {
    return D3D12Surface::Create(*m_device, windowParams, queue, maxFramesInFlight, m_debug, props);
}

GPUObjectResult<IGPUTexture> D3D12GPUDevice::CreateTexture(const TextureSpec &spec) {
    return D3D12Texture::Create(*m_device, spec);
}

GPUObjectResult<IGPUTextureView> D3D12GPUDevice::CreateTextureView(const TextureViewSpec &spec) {
    return D3D12TextureView::Create(*m_descMgr, spec);
}

GPUObjectResult<IGPUBuffer> D3D12GPUDevice::CreateBuffer(const BufferSpec &spec) {
    return D3D12Buffer::Create(*m_device, spec);
}

GPUObjectResult<IGPUBufferView> D3D12GPUDevice::CreateBufferView(const BufferViewSpec &spec) {
    return D3D12BufferView::Create(*m_descMgr, spec);
}

} // namespace ymir::gpu
