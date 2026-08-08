#pragma once

namespace app::gfxv2 {

#ifdef _WIN32
    #define YMIR_PLATFORM_HAS_DIRECT3D
#endif

#ifdef __APPLE__
    #define YMIR_PLATFORM_HAS_METAL
#endif

// TODO: check for Vulkan support on the platform
#define YMIR_PLATFORM_HAS_VULKAN

/// @brief Graphics backend options.
enum class Backend {
    Default,
#ifdef YMIR_PLATFORM_HAS_DIRECT3D
    Direct3D11,
    Direct3D12,
#endif
#ifdef YMIR_PLATFORM_HAS_METAL
    Metal,
#endif
#ifdef YMIR_PLATFORM_HAS_VULKAN
    Vulkan,
#endif
    SDLRenderer,
};

inline constexpr Backend kGraphicsBackends[] = {
    Backend::Default,
#ifdef YMIR_PLATFORM_HAS_DIRECT3D
    Backend::Direct3D11,  Backend::Direct3D12,
#endif
#ifdef YMIR_PLATFORM_HAS_METAL
    Backend::Metal,
#endif
#ifdef YMIR_PLATFORM_HAS_VULKAN
    Backend::Vulkan,
#endif
    Backend::SDLRenderer,
};

inline constexpr const char *GraphicsBackendName(Backend backend) {
    switch (backend) {
    case Backend::Default: return "Default";
#ifdef YMIR_PLATFORM_HAS_DIRECT3D
    case Backend::Direct3D11: return "Direct3D 11";
    case Backend::Direct3D12: return "Direct3D 12";
#endif
#ifdef YMIR_PLATFORM_HAS_METAL
    case Backend::Metal: return "Metal";
#endif
#ifdef YMIR_PLATFORM_HAS_VULKAN
    case Backend::Vulkan: return "Vulkan";
#endif
    case Backend::SDLRenderer: return "SDL Renderer";
    default: return "Default";
    }
}

} // namespace app::gfxv2
