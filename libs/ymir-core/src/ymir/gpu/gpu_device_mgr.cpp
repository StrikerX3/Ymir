#include <ymir/gpu/gpu_device_mgr.hpp>

#include <ymir/gpu/api/null/gpu_device_null.hpp>

#if YMIR_PLATFORM_HAS_DIRECT3D12
    #include <ymir/gpu/api/d3d12/d3d12_gpu_device.hpp>
#endif

#if YMIR_PLATFORM_HAS_VULKAN
// TODO: #include <ymir/gpu/api/vulkan/vulkan_gpu_device.hpp>
#endif

#if YMIR_PLATFORM_HAS_METAL
// TODO: #include <ymir/gpu/api/metal/metal_gpu_device.hpp>
#endif

namespace ymir::gpu {

GPUDeviceManager::GPUDeviceManager()
    : m_device(std::make_unique<NullGPUDevice>()) {}

GPUDeviceManager::~GPUDeviceManager() = default;

#if YMIR_PLATFORM_HAS_DIRECT3D12
GPUPointerResult<IGPUDevice> GPUDeviceManager::Create(const D3D12GPUDeviceSpec &spec) {
    auto result = D3D12GPUDevice::Create(spec);
    if (result) {
        m_device.swap(result.Value());
        return m_device.get();
    }
    return std::move(result.Error());
}
#endif

#if YMIR_PLATFORM_HAS_VULKAN
GPUPointerResult<IGPUDevice> GPUDeviceManager::Create(const VulkanGPUDeviceSpec &spec) {
    // TODO: implement
    return GPUOperationError{"Unimplemented"};
}
#endif

#if YMIR_PLATFORM_HAS_METAL
GPUPointerResult<IGPUDevice> GPUDeviceManager::Create(const MetalGPUDeviceSpec &spec) {
    // TODO: implement
    return GPUOperationError{"Unimplemented"};
}
#endif

void GPUDeviceManager::Destroy() {
    m_device = std::make_unique<NullGPUDevice>();
}

} // namespace ymir::gpu
