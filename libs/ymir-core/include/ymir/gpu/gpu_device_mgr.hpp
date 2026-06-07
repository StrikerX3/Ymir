#pragma once

#include <ymir/gpu/api/base/gpu_device.hpp>

#include <memory>

namespace ymir::gpu {

// ---------------------------------------------------------------------------------------------------------------------
// Forward declarations for GPUDevice creation specifications.
// See implementations in ymir/gpu/api/<api>/<api>_params.hpp and ymir/gpu/api/<api>/<api>_gpu_device.cpp/hpp.

#if YMIR_PLATFORM_HAS_DIRECT3D12
struct D3D12GPUDeviceSpec;
#endif

#if YMIR_PLATFORM_HAS_VULKAN
struct VulkanGPUDeviceSpec;
#endif

#if YMIR_PLATFORM_HAS_METAL
struct MetalGPUDeviceSpec;
#endif

// ---------------------------------------------------------------------------------------------------------------------
// GPU device manager

/// @brief A GPU device manager holds an instance of a GPU device.
class GPUDeviceManager {
public:
    GPUDeviceManager();
    ~GPUDeviceManager();

#if YMIR_PLATFORM_HAS_DIRECT3D12
    /// @brief Creates a Direct3D 12 GPU device with the given specifications.
    /// @param[in] spec GPU device creation specifications
    /// @return a pointer to the Direct3D 12 GPU device or an error
    GPUPointerResult<IGPUDevice> Create(const D3D12GPUDeviceSpec &spec);
#endif

#if YMIR_PLATFORM_HAS_VULKAN
    /// @brief Creates a Vulkan GPU device with the given specifications.
    /// @param[in] spec GPU device creation specifications
    /// @return a pointer to the Vulkan GPU device or an error
    GPUPointerResult<IGPUDevice> Create(const VulkanGPUDeviceSpec &spec);
#endif

#if YMIR_PLATFORM_HAS_METAL
    /// @brief Creates a Metal GPU device with the given specifications.
    /// @param[in] spec GPU device creation specifications
    /// @return a pointer to the Metal GPU device or an error
    GPUPointerResult<IGPUDevice> Create(const MetalGPUDeviceSpec &spec);
#endif

    /// @brief Destroys the GPU instance and releases all resources.
    /// Any existing references to objects created by this device become invalid once this function completes.
    void Destroy();

    /// @brief Returns a reference to the underlying `IGPUDevice` instance for convenient access to its operations.
    /// @return a reference to the current `IGPUDevice` instance held by this manager.
    IGPUDevice &operator->() {
        return *m_device.get();
    }

    /// @brief Returns a reference to the underlying `IGPUDevice` instance for convenient access to its operations.
    /// @return a reference to the current `IGPUDevice` instance held by this manager.
    const IGPUDevice &operator->() const {
        return *m_device.get();
    }

    /// @brief Returns a reference to the underlying `IGPUDevice` instance for convenient access to its operations.
    /// @return a reference to the current `IGPUDevice` instance held by this manager.
    IGPUDevice &operator*() {
        return *m_device.get();
    }

    /// @brief Returns a reference to the underlying `IGPUDevice` instance for convenient access to its operations.
    /// @return a reference to the current `IGPUDevice` instance held by this manager.
    const IGPUDevice &operator*() const {
        return *m_device.get();
    }

private:
    std::unique_ptr<IGPUDevice> m_device;
};

} // namespace ymir::gpu
