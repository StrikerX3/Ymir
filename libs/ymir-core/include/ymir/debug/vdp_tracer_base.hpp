#pragma once

/**
@file
@brief Defines `ymir::debug::IVDPTracer`, the VDP tracer interface.
*/

#include <ymir/hw/vdp/vdp_state.hpp>

#include <ymir/core/types.hpp>

namespace ymir::debug {

/// @brief Interface for VDP tracers.
///
/// Must be implemented by users of the core library.
///
/// Attach to an instance of `ymir::vdp::VDP` with its `UseTracer(IVDPTracer *)` method.
///
/// All functions document the thread they are invoked from. Functions can be either invoked from the emulator thread or
/// the VDP renderer thread.
///
/// @note This tracer requires the emulator to execute in debug tracing mode.
struct IVDPTracer {
    /// @brief Default virtual destructor. Required for inheritance.
    virtual ~IVDPTracer() = default;

    /// @brief Invoked when a new frame begins.
    ///
    /// A frame begins when the VDP2 enters the active display area at the top left of the screen.
    ///
    /// @param[in] state the VDP state at the beginning of the frame.
    ///
    /// This function is always invoked by the emulator thread.
    virtual void BeginFrame(const vdp::VDPState &state) {}
};

} // namespace ymir::debug
