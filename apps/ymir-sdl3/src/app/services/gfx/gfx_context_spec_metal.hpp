#pragma once

/**
@file
@brief Defines the contents of `app::gfx::MetalGraphicsContextSpec`.
*/

#include <SDL3/SDL_video.h>

namespace app::gfx {

/*!
 @enum MetalGPUFamily
 Similar to Apple's MTLGPUFamily enum. Used to set the minimum feature level
 the graphics context needs to run on.

 Intel/AMD Mac values and cross-platform tiers (Common1-3) are technically deprecated by Apple starting in macOS 27 SDK
 in favor of the Apple family values, but are kept here since we support Intel Macs.
*/
enum class MetalGPUFamily : uint32 {
    Apple1 = 1001,  // A7
    Apple2 = 1002,  // A8
    Apple3 = 1003,  // A9/A10
    Apple4 = 1004,  // A11
    Apple5 = 1005,  // A12
    Apple6 = 1006,  // A13
    Apple7 = 1007,  // A14, M1
    Apple8 = 1008,  // A15/A16, M2
    Apple9 = 1009,  // A17 Pro/A18, M3/M4
    Apple10 = 1010, // A19, M5

    Mac1 = 2001, // old Intel/AMD GCN cards
    Mac2 = 2002, // Iris Plus, Radeon Pro/Vega

    Common1 = 3001, // minimum supported by Metal
    Common2 = 3002, // mid-tier
    Common3 = 3003, // M1+ and modern discrete GPUs

    Metal3 = 5001, // macOS 13+
    Metal4 = 5002, // macOS 26+
};

struct MetalGraphicsContextSpec {
    /// @brief (Required) Target feature level.
    MetalGPUFamily featureLevel = MetalGPUFamily::Common1;

    /// @brief (Required) Pointer to SDL3 window
    SDL_Window *window = nullptr;

    /// @brief (Optional) Target Metal device pointer (id<MTLDevice>).
    /// Defaults to the system default Metal device if nullptr.
    void *device = nullptr;
};

} // namespace app::gfx
