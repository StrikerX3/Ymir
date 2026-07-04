#pragma once

/**
@file
@brief Common SCSI definitions and operations.
*/

#include <ymir/core/types.hpp>

#include <array>

namespace ymir::scsi {

// SCSI operation codes
namespace op {

    // GET EVENT/STATUS NOTIFICATION
    static constexpr uint8 kGetEventStatusNotification = 0x4A;

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

// Direction for SCSI data transfers
enum class Direction {
    In,    // SCSI device -> host
    Out,   // Host -> SCSI device
    InOut, // SCSI device <-> host
    None,  // No data transfers
};

// Notification class bits for GET EVENT/STATUS NOTIFICATION command
namespace notif_class {

    static constexpr uint8 kMediaStatus = 1u << 4u; // Media Status Class Events

} // namespace notif_class

} // namespace ymir::scsi
