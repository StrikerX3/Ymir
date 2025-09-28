#pragma once

/**
@file
@brief Defines `ymir::debug::ICDDriveTracer`, the LLE CD drive tracer interface.
*/

#include <ymir/core/types.hpp>

#include <span>

namespace ymir::debug {

/// @brief Interface for LLE CD drive tracers.
///
/// Must be implemented by users of the core library.
///
/// Attach to an instance of `ymir::cdblock::CDDrive` with its `UseTracer(ICDDriveTracer *)` method.
struct ICDDriveTracer {
    /// @brief Default virtual destructor. Required for inheritance.
    virtual ~ICDDriveTracer() = default;

    /// @brief Invoked when the CD drive receives a command through the serial interface.
    /// @param[in] command the command sent to the CD drive
    virtual void RxCommand(std::span<const uint8, 13> command) {}

    /// @brief Invoked when the CD drive transmits its status through the serial interface.
    /// @param[in] status the status sent to the SH-1
    virtual void TxStatus(std::span<const uint8, 13> status) {}
};

} // namespace ymir::debug
