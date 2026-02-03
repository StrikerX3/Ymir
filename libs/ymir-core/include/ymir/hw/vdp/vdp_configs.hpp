#pragma once

/**
@file
@brief VDP1/2 configuration definitions.
*/

#include <ymir/hw/vdp/vdp_callbacks.hpp>
#include <ymir/hw/vdp/vdp_defs.hpp>

#include <ymir/core/types.hpp>

#include <array>

namespace ymir::vdp::config {

/// @brief Graphics enhancements configuration.
struct Enhancements {
    /// @brief Enable or disable deinterlacing of double-density interlaced frames.
    /// When disabled, high resolution interlaced modes are rendered using the weave method.
    bool deinterlace = false;

    /// @brief Enable or disable transparent mesh rendering enhancement.
    /// When enabled, VDP1 sprites drawn in mesh mode are instead rendered to a separate sprite buffer that is blended
    /// with half-transparency on top of other graphics.
    bool transparentMeshes = false;
};

/// @brief VDP2 debug rendering options.
struct VDP2DebugRender {
    VDP2DebugRender() {
        enabledLayers.fill(true);
    }

    // Debug overlay alpha blended on top of the final composite image
    struct Overlay {
        enum class Type {
            None,        // No overlay is applied
            SingleLayer, // Display raw contents of a single layer
            LayerStack,  // Colorize by layer on a level of the stack
            Windows,     // Colorize by window state (one layer or custom setup)
            RotParams,   // Colorize by rotation parameters on RBG0
            ColorCalc,   // Colorize by color calculation flag/mode
            Shadow,      // Colorize by shadow flag
        } type = Type::None;

        // Whether to render the debug overlay.
        bool enable = false;

        // 8-bit opacity for overlay layer. 0=fully transparent, 255=fully opaque
        uint8 alpha = 128;

        // Which layer stack level to draw when using SingleLayer overlay.
        // [0] Sprite
        // [1] RBG0
        // [2] NBG0/RBG1
        // [3] NBG1/EXBG
        // [4] NBG2
        // [5] NBG3
        // [6] Back
        // [7] Line color
        // [8] Transparent mesh sprites (when enhancement is enabled)
        uint8 singleLayerIndex = 0;

        // Which layer stack level to draw when using LayerStack overlay.
        // 0=top, 1=middle, 2=bottom.
        // Any other value defaults to 0.
        uint8 layerStackIndex = 0;

        // Colors for each layer:
        // [0] Sprite
        // [1] RBG0
        // [2] NBG0/RBG1
        // [3] NBG1/EXBG
        // [4] NBG2
        // [5] NBG3
        // [6] Back
        // [7] Line color (never used)
        std::array<Color888, 8> layerColors{{
            {.r = 0xFF, .g = 0xFF, .b = 0xFF},
            {.r = 0x00, .g = 0xFF, .b = 0xFF},
            {.r = 0xFF, .g = 0x00, .b = 0xFF},
            {.r = 0x00, .g = 0x00, .b = 0xFF},
            {.r = 0xFF, .g = 0xFF, .b = 0x00},
            {.r = 0x00, .g = 0xFF, .b = 0x00},
            {.r = 0xFF, .g = 0x00, .b = 0x00},
            {.r = 0x00, .g = 0x00, .b = 0x00},
        }};

        // Which layer to display the window state of.
        // 0 = Sprite
        // 1 = RBG0
        // 2 = NBG0/RBG1
        // 3 = NBG1/EXBG
        // 4 = NBG2
        // 5 = NBG3
        // 6 = Rotation parameters
        // 7 = Color calculations
        // Any other value is interpreted as a custom mode
        uint8 windowLayerIndex = 0;

        WindowSet<true> customWindowSet{};
        std::array<bool, 2> customLineWindowTableEnable{};
        std::array<uint32, 2> customLineWindowTableAddress{};
        std::array<std::array<bool, vdp::kMaxResH>, 2> customWindowState{};

        Color888 windowInsideColor{.r = 0xFF, .g = 0xFF, .b = 0xFF};
        Color888 windowOutsideColor{.r = 0x00, .g = 0x00, .b = 0x00};

        Color888 rotParamAColor{.r = 0x00, .g = 0xFF, .b = 0xFF};
        Color888 rotParamBColor{.r = 0xFF, .g = 0x00, .b = 0xFF};

        // Which layer stack level to draw when using ColorCalc overlay.
        // 0=top, 1=middle.
        // Any other value defaults to 0.
        uint8 colorCalcStackIndex = 0;
        Color888 colorCalcDisableColor{.r = 0x00, .g = 0x00, .b = 0x00};
        Color888 colorCalcEnableColor{.r = 0xFF, .g = 0xFF, .b = 0xFF};

        Color888 shadowDisableColor{.r = 0xFF, .g = 0xFF, .b = 0xFF};
        Color888 shadowEnableColor{.r = 0x00, .g = 0x00, .b = 0x00};
    } overlay;

    /// @brief Whether to enable or disable (forcibly hide) layers.
    /// Enabled layers are rendered normally. Disabled layers are always hidden.
    /// Each index corresponds to a layer depending on which RBGs are enabled:
    /// ```
    ///     RBG0+RBG1   RBG0        RBG1        no RBGs
    /// [0] Sprite      Sprite      Sprite      Sprite
    /// [1] RBG0        RBG0        -           -
    /// [2] RBG1        NBG0        RBG1        NBG0
    /// [3] EXBG        NBG1/EXBG   NBG1/EXBG   NBG1/EXBG
    /// [4] -           NBG2        NBG2        NBG2
    /// [5] -           NBG3        NBG3        NBG3
    /// ```
    std::array<bool, 6> enabledLayers;
};

/// @brief Frontend renderer callbacks.
struct RendererCallbacks {
    // Invoked when the VDP1 finishes drawing a frame.
    CBVDP1DrawFinished VDP1DrawFinished;

    // Invoked when the VDP1 swaps framebuffers.
    CBVDP1FramebufferSwap VDP1FramebufferSwap;

    // Invoked when the VDP2 finishes drawing a frame.
    CBVDP2DrawFinished VDP2DrawFinished;
};

} // namespace ymir::vdp::config
