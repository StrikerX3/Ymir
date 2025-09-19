#pragma once

/**
@file
@brief Scheduler event definitions.
*/

#include <ymir/core/types.hpp>

namespace ymir::core {

using IDtype = uint8;       ///< The ID type used for scheduler events
using EventID = IDtype;     ///< The event ID type
using UserEventID = IDtype; ///< The user ID type (for save states)

namespace events {

    inline constexpr UserEventID VDPPhase = 0x10;             ///< VDP phase update
    inline constexpr UserEventID SCSPSample = 0x20;           ///< SCSP sample tick
    inline constexpr UserEventID CDBlockDriveState = 0x30;    ///< CD block drive state update
    inline constexpr UserEventID CDBlockCommand = 0x31;       ///< CD block command processing
    inline constexpr UserEventID CDBlockLLEDriveState = 0x32; ///< CD block LLE drive state update
    inline constexpr UserEventID SCUTimer1 = 0x40;            ///< SCU timer 1 interrupt
    inline constexpr UserEventID SMPCCommand = 0x50;          ///< SMPC command processing

} // namespace events

/// @brief The total number of schedulable events.
///
/// Must match the number of events defined in the `ymir::core::events` namespace.
inline constexpr size_t kNumScheduledEvents = 7;

} // namespace ymir::core
