#pragma once

#include <ymir/hw/vdp/renderer/vdp_renderer_base.hpp>

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

    /// @brief VDP1 VRAM synchronization interval.
    VDP1VRAMSyncInterval VDP1VRAMSyncInterval = VDP1VRAMSyncInterval::Command;

    /// @brief VDP2 VRAM synchronization interval.
    VDP2VRAMSyncInterval VDP2VRAMSyncInterval = VDP2VRAMSyncInterval::Scanline;

    // -------------------------------------------------------------------------
    // Type casting and information

    HardwareVDPRendererBase *AsHardwareRenderer() override {
        return this;
    }
};

} // namespace ymir::vdp
