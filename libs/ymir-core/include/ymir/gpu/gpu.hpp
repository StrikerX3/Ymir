#pragma once

/**
@file
@brief Includes all Ymir graphics API functionality.

Ymir's graphics API supports the following APIs:
- Direct3D 12 on Windows
- Metal on macOS
- Vulkan on all platforms

# API usage

<!-- TODO: describe API usage, starting with physical device enumeration and IGPUDevice instantiation -->

## Accessing raw graphics API objects

@warning Mixing managed and raw graphics APIs requires extra care to ensure proper synchronization between the two
layers. Avoid modifying or destroying managed objects with raw graphics APIs.

If you need to direct access to platform-specific graphics API objects, use the pattern below:

```cpp
IGPUDevice &device = ...;
if (auto *vkDevice = device.As<VulkanDevice>()) { // or D3D12Device, or MetalDevice, etc.
    // If the device instance is a `VulkanDevice`, vkDevice is a valid pointer to it in this scope.
    VkInstance instance = vkDevice->GetInstance();
    VkPhysicalDevice device = vkDevice->GetPhysicalDevice();
    // Do whatever you need with the Vulkan API
}
```

This can be used with any object from Ymir's graphics API.
*/

#include "gpu_device_mgr.hpp"
#include "gpu_shaders.hpp"
