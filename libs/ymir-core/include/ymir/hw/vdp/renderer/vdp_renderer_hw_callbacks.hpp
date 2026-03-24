#pragma once

#include <ymir/util/callback.hpp>

#include <ymir/core/types.hpp>

namespace ymir::vdp {

// -----------------------------------------------------------------------------
// Forward declarations

class IVDPRenderer;

// -----------------------------------------------------------------------------

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

/// @brief Type of callback invoked when the hardware output texture is created.
/// Can be used to update texture references if cached or wrapped by other frontend objects.
/// This callback is invoked by the emulator or renderer thread.
///
/// @param[in] renderer a reference to the VDP renderer
/// @param[in] texture a pointer to the texture object specific to the graphics API in use
/// @param[in] width the new texture width
/// @param[in] height the new texture height
using CBHardwareOutputTextureCreated =
    util::OptionalCallback<void(IVDPRenderer &renderer, void *texture, uint32 width, uint32 height)>;

/// @brief Type of callback invoked when the hardware output texture is destroyed.
/// Can be used to update texture references if cached or wrapped by other frontend objects.
/// This callback is invoked by the emulator or renderer thread.
///
/// @param[in] renderer a reference to the VDP renderer
/// @param[in] texture a pointer to the texture object specific to the graphics API in use
using CBHardwareOutputTextureDestroyed = util::OptionalCallback<void(IVDPRenderer &renderer, void *texture)>;

/// @brief Callbacks specific to hardware VDP renderers.
struct HardwareRendererCallbacks {
    /// @brief Callback invoked when a command list is prepared.
    /// This callback is invoked by the renderer thread (which may be the emulator thread or a dedicated thread).
    CBHardwareCommandListReady CommandListReady;

    /// @brief Callback invoked before a command list is processed.
    /// This callback is invoked by the thread that invokes `HardwareVDPRendererBase::ExecutePendingCommandLists()`.
    CBHardwarePreExecuteCommandList PreExecuteCommandList;

    /// @brief Callback invoked after a command list is processed.
    /// This callback is invoked by the thread that invokes `HardwareVDPRendererBase::ExecutePendingCommandLists()`.
    CBHardwarePostExecuteCommandList PostExecuteCommandList;

    /// @brief Callback invoked when the hardware output texture is created.
    /// This callback is invoked by the renderer thread (which may be the emulator thread or a dedicated thread).
    CBHardwareOutputTextureCreated OutputTextureCreated;

    /// @brief Callback invoked when the hardware output texture is destroyed.
    /// This callback is invoked by the renderer thread (which may be the emulator thread or a dedicated thread).
    CBHardwareOutputTextureDestroyed OutputTextureDestroyed;
};

} // namespace ymir::vdp
