#pragma once

#include "gfx_types.hpp"

#include <ymir/core/types.hpp>

#include <functional>

namespace app::gfx {

/// @brief A handle to a texture managed by the graphics service.
using GUITextureHandle = uint32;

/// @brief A sentinel value representing an invalid texture handle.
inline constexpr GUITextureHandle kInvalidGUITextureHandle = 0xFFFFFFFF;

/// @brief Function type for setting up a texture.
/// @param[in] handle the texture handle
/// @param[in] recreate `false` if the texture is being created for the first time, `true` if it is being recreated
/// after initializing a new graphics context
/// @param[in] data pointer to writable texture data
/// @param[in] pitch stride of each line in bytes
using FnSetup = std::function<void(GUITextureHandle handle, bool recreate, void *data, size_t pitch)>;

} // namespace app::gfx
