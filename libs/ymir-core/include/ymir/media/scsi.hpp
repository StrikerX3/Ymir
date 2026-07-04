#pragma once

/**
@file
@brief Common SCSI definitions and operations.
*/

#include <ymir/core/types.hpp>

#include <array>

namespace ymir::scsi {

enum class MediaPresenceState { Absent, Present, Unknown };
enum class TrayState { Open, Closed, Unknown };

/// @brief SCSI operation codes
namespace op {

    /// @brief SCSI operation code for GET EVENT/STATUS NOTIFICATION
    static constexpr uint8 kGetEventStatusNotification = 0x4A;

    /// @brief Build a GET EVENT/STATUS NOTIFICATION command descriptor block.
    /// @param[in] immed whether to poll immediately (`true`) or run asynchronously (`false`). Corresponds to the
    /// command's IMMED flag (bit 0 of byte 0).
    /// @param[in] classEvents bitmask of class events to be notified. Use the constants defined in the
    /// `ymir::scsi::notif_class` namespace.
    /// @param[in] length size of the output buffer
    /// @return an array with the command descriptor block for a GET EVENT/STATUS NOTIFICATION command built from the
    /// given parameters
    static std::array<uint8, 10> MakeGetEventStatusNotification(bool immed, uint8 classEvents, uint16 length) {
        std::array<uint8, 10> cdb{};
        cdb[0] = kGetEventStatusNotification;
        cdb[1] = immed ? 1 : 0; // bit 0 = IMMED
        cdb[4] = classEvents;   // Media class events (bit 4)
        cdb[7] = length >> 8u;  // Allocation length (MSB)
        cdb[8] = length >> 0u;  // Allocation length (LSB)
        return cdb;
    }

} // namespace op

/// @brief Direction for SCSI data transfers.
enum class Direction {
    In,    // SCSI device -> host
    Out,   // Host -> SCSI device
    InOut, // SCSI device <-> host
    None,  // No data transfers
};

/// @brief Notification class bits for the GET EVENT/STATUS NOTIFICATION command.
namespace notif_class {

    static constexpr uint8 kMediaStatus = 1u << 4u; ///< Media Status Class Events

} // namespace notif_class

} // namespace ymir::scsi
