#pragma once

#include <ymir/core/types.hpp>

namespace ymir::gpu {

/// @brief OS window parameters for creating GPU surfaces.
struct WindowParams {
    /// @brief OS-specific window handle.
    /// - HWND on Windows
    /// - (TODO: other platforms)
    void *windowHandle = nullptr;

    /// @brief OS-specific surface handle, for systems that use a separate surface descriptor (such as on macOS).
    void *surfaceHandle = nullptr;

    /// @brief Width of the surface.
    uint32 width;

    /// @brief Height of the surface.
    uint32 height;
};

} // namespace ymir::gpu
