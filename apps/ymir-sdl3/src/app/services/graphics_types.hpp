#pragma once

#include <ymir/core/types.hpp>

#include <SDL3/SDL_render.h>

#include <functional>

namespace app::gfx {

#ifdef _WIN32
    #define YMIR_PLATFORM_HAS_DIRECT3D
#endif

#ifdef __APPLE__
    #define YMIR_PLATFORM_HAS_METAL
#endif

#define YMIR_PLATFORM_HAS_VULKAN

// OpenGL is offered as a fallback option only, not recommended
#if !defined(__APPLE__) && !defined(_WIN32)
    #define YMIR_PLATFORM_HAS_OPENGL
#endif

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
#ifdef YMIR_PLATFORM_HAS_OPENGL
    OpenGL,
#endif
};

inline constexpr Backend kGraphicsBackends[] = {
    Backend::Default,
#ifdef YMIR_PLATFORM_HAS_DIRECT3D
    Backend::Direct3D11, Backend::Direct3D12,
#endif
#ifdef YMIR_PLATFORM_HAS_METAL
    Backend::Metal,
#endif
#ifdef YMIR_PLATFORM_HAS_VULKAN
    Backend::Vulkan,
#endif
#ifdef YMIR_PLATFORM_HAS_OPENGL
    Backend::OpenGL,
#endif
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
#ifdef YMIR_PLATFORM_HAS_OPENGL
    case Backend::OpenGL: return "OpenGL";
#endif
    default: return "Default";
    }
}

inline constexpr const char *GraphicsBackendRendererID(Backend backend) {
    switch (backend) {
    case Backend::Default: return nullptr;
#ifdef YMIR_PLATFORM_HAS_DIRECT3D
    case Backend::Direct3D11: return "direct3d11";
    case Backend::Direct3D12: return "direct3d12";
#endif
#ifdef YMIR_PLATFORM_HAS_METAL
    case Backend::Metal: return "metal";
#endif
#ifdef YMIR_PLATFORM_HAS_VULKAN
    case Backend::Vulkan: return "vulkan";
#endif
#ifdef YMIR_PLATFORM_HAS_OPENGL
    case Backend::OpenGL: return "opengl";
#endif
    default: return nullptr;
    }
}

/// @brief A handle to a texture managed by the graphics service.
using TextureHandle = uint32;

/// @brief A value representing an invalid texture handle.
inline constexpr TextureHandle kInvalidTextureHandle = 0u;

/// @brief Type of function invoked when a texture is created to setup texture parameters, reload texture data, etc.
/// @param[in] texture a pointer to the texture being created
/// @param[in] recreated whether the texture is being newly created (`false`) or recreated from existing parameters
/// (`true`)
using FnTextureSetup = std::function<void(SDL_Texture *texture, bool recreated)>;

} // namespace app::gfx
