#pragma once

#include <ymir/util/callback.hpp>

namespace ymir::vdp {

/// @brief Type of callback invoked when a command list is ready to be processed.
/// This callback is invoked by the emulator or renderer thread.
/// @param[in] vdp2FrameEnd whether the command being processed is the end of a VDP2 frame
using CBHardwareCommandListReady = util::OptionalCallback<void(bool vdp2FrameEnd)>;

/// @brief Type of callback invoked immediately before executing a command list.
/// Can be used to setup the graphics system, flush commands, preserve state, etc.
/// This callback is invoked in the same thread that invokes `ExecutePendingCommandList()`.
/// @param[in] first `true` if processing the first command in the list
using CBHardwarePreExecuteCommandList = util::OptionalCallback<void(bool first)>;

/// @brief Type of callback invoked immediately after executing a command list.
/// Can be used to cleanup resources, restore state, measure time, etc.
/// This callback is invoked in the same thread that invokes `ExecutePendingCommandList()`.
/// @param[in] last `true` if processing the last command in the list
using CBHardwarePostExecuteCommandList = util::OptionalCallback<void(bool last)>;

/// @brief Type of callback invoked when the output texture is recreated.
/// Can be used to update texture references if cached or wrapped by other frontend objects.
/// This callback is invoked by the emulator or renderer thread.
using CBHardwareOutputTextureRecreated = util::OptionalCallback<void()>;

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

    /// @brief Callback invoked when the output texture is recreated. This callback is invoked by the renderer thread
    /// (which may be the emulator thread or a dedicated thread).
    CBHardwareOutputTextureRecreated OutputTextureRecreated;
};

} // namespace ymir::vdp
