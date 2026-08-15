#pragma once

#include <ymir/hw/vdp/renderer/vdp_renderer_base.hpp>

#include <ymir/core/configuration.hpp>

namespace ymir::vdp {

/// @brief Base type for all hardware renderers.
/// Defines some hardware rendere specific features and functions.
class HardwareVDPRendererBase : public IVDPRenderer {
public:
    HardwareVDPRendererBase(VDPRendererType type, core::Configuration::HardwareRenderer &hwRenderConfig)
        : IVDPRenderer(type)
        , HwRenderConfig(hwRenderConfig) {}

    virtual ~HardwareVDPRendererBase() = default;

    // -------------------------------------------------------------------------
    // Basics

    bool IsHardwareRenderer() const override {
        return true;
    }

    // -------------------------------------------------------------------------
    // Configuration

    /// @brief Hardware VDP renderer configuration.
    core::Configuration::HardwareRenderer &HwRenderConfig;

    // -------------------------------------------------------------------------
    // Type casting and information

    HardwareVDPRendererBase *AsHardwareRenderer() override {
        return this;
    }
};

} // namespace ymir::vdp
