#pragma once

/**
@file
@brief Defines `ymir::debug::IYGRTracer`, the LLE CD drive tracer interface.
*/

#include <ymir/core/types.hpp>

#include <span>

namespace ymir::debug {

/// @brief Interface for YGR tracers.
///
/// Must be implemented by users of the core library.
///
/// Attach to an instance of `ymir::cdblock::YGR` with its `UseTracer(IYGRTracer *)` method.
struct IYGRTracer {
    /// @brief Default virtual destructor. Required for inheritance.
    virtual ~IYGRTracer() = default;

    /// @brief Invoked when the YGR receives a command from the host (SH-2).
    /// @param[in] cr1 the value of CR1
    /// @param[in] cr2 the value of CR2
    /// @param[in] cr3 the value of CR3
    /// @param[in] cr4 the value of CR4
    virtual void ReceiveHostCommand(uint16 cr1, uint16 cr2, uint16 cr3, uint16 cr4) {}

    /// @brief Invoked when the YGR receives a response from the SH-1. Also invoked for periodic status reports.
    /// @param[in] rr1 the value of RR1
    /// @param[in] rr2 the value of RR2
    /// @param[in] rr3 the value of RR3
    /// @param[in] rr4 the value of RR4
    virtual void ReceiveCDBlockResponse(uint16 rr1, uint16 rr2, uint16 rr3, uint16 rr4) {}
};

} // namespace ymir::debug
