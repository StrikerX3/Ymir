#pragma once

#ifdef _WIN32
    #define YMIR_PLATFORM_HAS_DIRECT3D
#endif

#ifdef __APPLE__
    #define YMIR_PLATFORM_HAS_METAL
#endif

// TODO: check for Vulkan support on the platform
#define YMIR_PLATFORM_HAS_VULKAN
