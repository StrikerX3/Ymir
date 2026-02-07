#pragma once

/**
@file
@brief Defines `ymir::debug::ISCSPTracer`, the SCSP tracer interface.
*/

#include <ymir/core/types.hpp>

namespace ymir::debug {

/// @brief Interface for SCSP tracers.
///
/// Must be implemented by users of the core library.
///
/// Attach to an instance of `ymir::cdblock::CDBlock` with its `UseTracer(ISCSPTracer *)` method.
struct ISCSPTracer {
    /// @brief Default virtual destructor. Required for inheritance.
    virtual ~ISCSPTracer() = default;

    /// @brief Invoked for each slot when the SCSP outputs a sample.
    /// @param[in] index the slot index
    /// @param[in] sample the slot output sample
    virtual void SlotSample(uint32 index, sint16 sample) = 0;

    /// @brief Invoked when KYONEX is processed.
    /// @param[in] slotsMask a bitmask with state of KYONB for each slot
    virtual void KeyOnExecute(uint32 slotsMask) = 0;
};

} // namespace ymir::debug
