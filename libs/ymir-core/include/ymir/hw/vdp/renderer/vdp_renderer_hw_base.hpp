#pragma once

#include <ymir/hw/vdp/renderer/vdp_renderer_base.hpp>

#include <ymir/util/callback.hpp>

namespace ymir::vdp {

/// @brief Type of callback invoked immediately before executing a command list, if one is pending.
/// Can be used to setup the graphics system, flush commands, preserve state, etc.
using CBHardwarePreExecuteCommandList = util::OptionalCallback<void()>;

/// @brief Type of callback invoked immediately after executing a command list, if one is pending.
/// Can be used to cleanup graphics resources, restore state, etc.
using CBHardwarePostExecuteCommandList = util::OptionalCallback<void()>;

/// @brief Callbacks specific to hardware VDP renderers.
struct HardwareRendererCallbacks {
    CBHardwarePreExecuteCommandList PreExecuteCommandList;
    CBHardwarePostExecuteCommandList PostExecuteCommandList;
};

// -----------------------------------------------------------------------------

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

    HardwareRendererCallbacks HwCallbacks;

    // -------------------------------------------------------------------------
    // Hardware rendering

    /// @brief Executes the latest pending command list if available in the immediate context of the `ID3D11Device`
    /// bound to this renderer.
    ///
    /// @return `true` if a command list was processed, `false` otherwise.
    virtual bool ExecutePendingCommandList() = 0;
};

} // namespace ymir::vdp
