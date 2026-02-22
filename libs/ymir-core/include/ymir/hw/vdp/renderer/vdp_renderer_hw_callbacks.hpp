#pragma once

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
    /// invokes `HardwareVDPRendererBase::ExecutePendingCommandLists()`.
    CBHardwarePreExecuteCommandList PreExecuteCommandList;

    /// @brief Callback invoked after a command list is processed. This callback is invoked by the same thread that
    /// invokes `HardwareVDPRendererBase::ExecutePendingCommandLists()`.
    CBHardwarePostExecuteCommandList PostExecuteCommandList;
};

} // namespace ymir::vdp
