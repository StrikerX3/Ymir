#pragma once

#include <ymir/hw/vdp/renderer/vdp_renderer_base.hpp>

#include "vdp_renderer_hw_callbacks.hpp"
#include "vdp_renderer_hw_defs.hpp"

namespace ymir::vdp {

/// @brief Base type for all hardware renderers.
/// Defines some hardware rendere specific features and functions.
class HardwareVDPRendererBase : public IVDPRenderer {
public:
    HardwareVDPRendererBase(VDPRendererType type)
        : IVDPRenderer(type) {}

    virtual ~HardwareVDPRendererBase() = default;

    // -------------------------------------------------------------------------
    // Basics

    bool IsHardwareRenderer() const override {
        return true;
    }

    // -------------------------------------------------------------------------
    // Configuration

    /// @brief Hardware renderer-specific callbacks.
    HardwareRendererCallbacks HwCallbacks;

    /// @brief VDP1 VRAM synchronization mode.
    VDP1VRAMSyncMode VDP1VRAMSyncMode = VDP1VRAMSyncMode::Command;

    /// @brief VDP2 VRAM synchronization mode.
    VDP2VRAMSyncMode VDP2VRAMSyncMode = VDP2VRAMSyncMode::Scanline;

    // -------------------------------------------------------------------------
    // Hardware rendering

    /// @brief Executes all pending command lists.
    ///
    /// The `HwCallbacks.PreExecuteCommandList` and `HwCallbacks.PostExecuteCommandList` callbacks are invoked before
    /// and after executing each command list.
    virtual void ExecutePendingCommandLists() = 0;

    /// @brief Discards all pending command lists.
    ///
    /// No callbacks are invoked.
    virtual void DiscardPendingCommandLists() = 0;

    // -------------------------------------------------------------------------
    // Type casting and information

    HardwareVDPRendererBase *AsHardwareRenderer() override {
        return this;
    }
};

} // namespace ymir::vdp
