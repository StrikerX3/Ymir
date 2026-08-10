#pragma once

#include <ymir/core/types.hpp>

namespace app::gfx {

/// @brief Graphics backend options.
enum class Backend {
    Null,
#if YMIR_PLATFORM_HAS_DIRECT3D
    Direct3D11,
    Direct3D12,
#endif
#if YMIR_PLATFORM_HAS_METAL
    Metal,
#endif
#if YMIR_PLATFORM_HAS_VULKAN
    Vulkan,
#endif
    SDLRenderer,
};

/// @brief A list of all backends available on this host system.
inline constexpr Backend kGraphicsBackends[] = {
    Backend::Null,
#if YMIR_PLATFORM_HAS_DIRECT3D
    Backend::Direct3D11,  Backend::Direct3D12,
#endif
#if YMIR_PLATFORM_HAS_METAL
    Backend::Metal,
#endif
#if YMIR_PLATFORM_HAS_VULKAN
    Backend::Vulkan,
#endif
    Backend::SDLRenderer,
};

/// @brief The preferred default backend for this host system.
inline constexpr Backend kDefaultBackend =
#if defined(YMIR_PLATFORM_HAS_DIRECT3D)
    Backend::Direct3D12;
#elif defined(YMIR_PLATFORM_HAS_METAL)
    Backend::Metal;
#elif defined(YMIR_PLATFORM_HAS_VULKAN)
    Backend::Vulkan;
#else
    Backend::SDLRenderer;
#endif

/// @brief Retrieves a human-readable name for the backend.
/// @param[in] backend the backend type
/// @return the backend name
inline constexpr const char *GraphicsBackendName(Backend backend) {
    switch (backend) {
    default: [[fallthrough]];
    case Backend::Null: return "Null";
#if YMIR_PLATFORM_HAS_DIRECT3D
    case Backend::Direct3D11: return "Direct3D 11";
    case Backend::Direct3D12: return "Direct3D 12";
#endif
#if YMIR_PLATFORM_HAS_METAL
    case Backend::Metal: return "Metal";
#endif
#if YMIR_PLATFORM_HAS_VULKAN
    case Backend::Vulkan: return "Vulkan";
#endif
    case Backend::SDLRenderer: return "SDL Renderer";
    }
}

// -----------------------------------------------------------------------------

/// @brief Graphics presentation modes.
enum class PresentMode {
    VSync,    ///< Synchronize to vertical retrace
    Adaptive, ///< Adjusts display refresh rate to match presentation speed (variable refresh rate)
    Mailbox,  ///< Presents immediately; may or may not tear
};

enum class PixelFormat {
    Unknown,

    XBGR8888,
    ABGR8888,

    // TODO: add formats as needed
};

/// @brief A point's coordinates in 2D space using floating point values.
struct FPoint2D {
    float x, y;
};

/// @brief A rectangle specification using unsigned integers for the top-left origin coordinate and the dimensions.
struct IRect {
    uint32 x, y;
    uint32 w, h;
};

/// @brief A rectangle specification using floating point values for the top-left origin coordinate and the dimensions.
struct FRect {
    float x, y;
    float w, h;
};

/// @brief RGBA color specification.
struct ColorRGBA {
    float r, g, b, a;
};

// -----------------------------------------------------------------------------

enum class TextureAccess {
    Static,       ///< Texture data uploaded on creation, cannot be changed later
    Streaming,    ///< Texture data can be changed at any point
    RenderTarget, ///< Texture can be used as render target
};

enum class TextureFilterMode {
    Nearest,
    Linear,
};

/// @brief A texture identifier, used for operations with textures on a graphics context.
/// This ID is immutable for the lifetime of the texture, even when resized.
using TextureID = uintptr_t;

/// @brief Texture format specifications.
struct Texture2DSpec {
    /// @brief Width of the texture.
    uint32 width = 0;

    /// @brief Height of the texture
    uint32 height = 0;

    /// @brief Texel format.
    PixelFormat format = PixelFormat::Unknown;

    /// @brief Texture access mode.
    TextureAccess access = TextureAccess::Static;

    /// @brief Texture magnification and minification filter mode.
    TextureFilterMode filterMode = TextureFilterMode::Linear;
};

} // namespace app::gfx
