#pragma once

#include <ymir/core/types.hpp>

namespace app::gfxv2 {

/// @brief A handle to a texture managed by the graphics service.
using TextureHandle = uint32;

/// @brief A value representing an invalid texture handle.
inline constexpr TextureHandle kInvalidTextureHandle = 0xFFFFFFFFu;

/// @brief Interface for platform graphics contexts.
/// Creates and manages a rendering context and grants access to raw API objects for more advanced graphics operations.
/// Use one of the platform factory methods to create one.
class IGraphicsContext {
public:
};

} // namespace app::gfxv2
