#pragma once

/**
@file
@brief Defines factory functions for creating platform-specific graphics contexts.

To use them, you will need to `#include` the appropriate platform-specific graphics context headers.
*/

#include "gfx_context.hpp"
#include "gfx_result.hpp"

namespace app::gfxv2 {

// -------------------------------------
// Direct3D 11 (Windows only)

#ifdef YMIR_PLATFORM_HAS_DIRECT3D
class Direct3D11GraphicsContext;
struct Direct3D11GraphicsContextSpec;

/// @brief Creates a Direct3D 11 graphics context.
/// @param[in] spec the backend specifications
/// @return the graphics context instance or an error message
GfxObjectResult<Direct3D11GraphicsContext> Create(const Direct3D11GraphicsContextSpec &spec);
#endif

// -------------------------------------
// Direct3D 12 (Windows only)

#ifdef YMIR_PLATFORM_HAS_DIRECT3D
class Direct3D12GraphicsContext;
struct Direct3D12GraphicsContextSpec;

/// @brief Creates a Direct3D 12 graphics context.
/// @param[in] spec the backend specifications
/// @return the graphics context instance or an error message
GfxObjectResult<Direct3D12GraphicsContext> Create(const Direct3D12GraphicsContextSpec &spec);
#endif

// -------------------------------------
// Vulkan

#ifdef YMIR_PLATFORM_HAS_VULKAN
class VulkanGraphicsContext;
struct VulkanGraphicsContextSpec;

/// @brief Creates a Vulkan graphics context.
/// @param[in] spec the backend specifications
/// @return the graphics context instance or an error message
GfxObjectResult<VulkanGraphicsContext> Create(const VulkanGraphicsContextSpec &spec);
#endif

// -------------------------------------
// Metal (macOS only)

#ifdef YMIR_PLATFORM_HAS_METAL
class MetalGraphicsContext;
struct MetalGraphicsContextSpec;

/// @brief Creates a Metal graphics context.
/// @param[in] spec the backend specifications
/// @return the graphics context instance or an error message
GfxObjectResult<MetalGraphicsContext> Create(const MetalGraphicsContextSpec &spec);
#endif

// -------------------------------------
// SDL Renderer (fallback)

class SDLRendererGraphicsContext;
struct SDLRendererGraphicsContextSpec;

/// @brief Creates a SDL Renderer graphics context.
/// @param[in] spec the backend specifications
/// @return the graphics context instance or an error message
GfxObjectResult<SDLRendererGraphicsContext> Create(const SDLRendererGraphicsContextSpec &spec);

} // namespace app::gfxv2
