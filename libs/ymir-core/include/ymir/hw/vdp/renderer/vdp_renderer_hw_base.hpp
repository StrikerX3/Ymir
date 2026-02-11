#pragma once

#include <ymir/hw/vdp/renderer/vdp_renderer_base.hpp>

#include <ymir/util/callback.hpp>

namespace ymir::vdp {

/// @brief Type of callback invoked when a command list is ready to be processed.
/// This callback is invoked by the emulator or renderer thread.
using CBHardwareCommandListReady = util::OptionalCallback<void()>;

/// @brief Type of callback invoked immediately before executing a command list.
/// Can be used to setup the graphics system, flush commands, preserve state, etc.
/// This callback is invoked in the same thread that invokes `ExecutePendingCommandList()`.
using CBHardwarePreExecuteCommandList = util::OptionalCallback<void()>;

/// @brief Type of callback invoked immediately after executing a command list.
/// Can be used to cleanup resources, restore state, measure time, etc.
/// This callback is invoked in the same thread that invokes `ExecutePendingCommandList()`.
using CBHardwarePostExecuteCommandList = util::OptionalCallback<void()>;

/// @brief Callbacks specific to hardware VDP renderers.
struct HardwareRendererCallbacks {
    /// @brief Callback invoked when a command list is prepared. This callback is invoked by the renderer thread (which
    /// may be the emulator thread or a dedicated thread).
    CBHardwareCommandListReady CommandListReady;

    /// @brief Callback invoked before a command list is processed. This callback is invoked by the same thread that
    /// invokes `HardwareVDPRendererBase::ExecutePendingCommandList()`.
    CBHardwarePreExecuteCommandList PreExecuteCommandList;

    /// @brief Callback invoked after a command list is processed. This callback is invoked by the same thread that
    /// invokes `HardwareVDPRendererBase::ExecutePendingCommandList()`.
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

    /// @brief Hardware renderer-specific callbacks.
    HardwareRendererCallbacks HwCallbacks;

    // -------------------------------------------------------------------------
    // Hardware rendering

    /// @brief Executes all pending command lists.
    ///
    /// The `HwCallbacks.PreExecuteCommandList` and `HwCallbacks.PostExecuteCommandList` callbacks are invoked before
    /// and after executing each command list.
    virtual void ExecutePendingCommandList() = 0;
};

} // namespace ymir::vdp
