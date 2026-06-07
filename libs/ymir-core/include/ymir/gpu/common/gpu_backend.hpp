#pragma once

/**
@file
@brief Defines the `Backend` enumeration of supported graphics APIs and utility functions to operate on them.
*/

#ifdef _WIN32
    #define YMIR_PLATFORM_HAS_DIRECT3D12 1
#else
    #define YMIR_PLATFORM_HAS_DIRECT3D12 0
#endif

#ifdef __APPLE__
    #define YMIR_PLATFORM_HAS_METAL 1
#else
    #define YMIR_PLATFORM_HAS_METAL 0
#endif

#define YMIR_PLATFORM_HAS_VULKAN 1

namespace ymir::gpu {

/// @brief Specifies a graphics backend.
enum class Backend {
    Default,    ///< Maps to Direct3D12 on Windows, Metal on macOS, Vulkan everywhere else
    Direct3D12, ///< Direct3D 12 - Windows-only, DXIL shaders
    Vulkan,     ///< Vulkan - all OSes, SPIR-V shaders
    Metal,      ///< Metal - macOS, Metal IR shaders
    Null,       ///< Null - all OSes, no shaders
};

/// @brief The default backend for the current platform:
/// - Direct3D 12 on Windows
/// - Metal on macOS
/// - Vulkan on every other OS
/// @return the default graphics backend
inline constexpr Backend kDefaultBackend =
#if defined(_WIN32)
    Backend::Direct3D12;
#elif defined(__APPLE__)
    Backend::Metal;
#else
    Backend::Vulkan;
#endif

/// @brief Returns a human-readable name for the backend.
/// @param[in] backend the backend
/// @return a human-readable name for the backend
constexpr const char *BackendName(Backend backend) {
    switch (backend) {
    case Backend::Default: return BackendName(kDefaultBackend);
    case Backend::Direct3D12: return "Direct3D 12";
    case Backend::Vulkan: return "Vulkan";
    case Backend::Metal: return "Metal";
    case Backend::Null: return "Null";
    default: return "Invalid";
    }
}

/// @brief Determines if a graphics backend is supported by the current platform.
/// This does not check for GPU and driver compatibility. It merely states if the API can be used on the current
/// platform.
///
/// @param[in] backend the graphics backend to check
/// @return `true` if supported, `false` otherwise.
constexpr bool IsBackendSupported(Backend backend) {
    switch (backend) {
    case Backend::Default: return IsBackendSupported(kDefaultBackend);
    case Backend::Direct3D12: return YMIR_PLATFORM_HAS_DIRECT3D12;
    case Backend::Vulkan: return YMIR_PLATFORM_HAS_VULKAN;
    case Backend::Metal: return YMIR_PLATFORM_HAS_METAL;
    case Backend::Null: return true;
    default: return false;
    }
}

} // namespace ymir::gpu
