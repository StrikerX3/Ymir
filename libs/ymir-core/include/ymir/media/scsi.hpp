#pragma once

/**
@file
@brief Common SCSI definitions and operations.
*/

#include <ymir/util/bit_ops.hpp>

#include <ymir/core/types.hpp>

#include <array>

namespace ymir::scsi {

/// @brief SCSI operation codes
namespace op {

    /// @brief SCSI operation code for INQUIRY
    inline constexpr uint8 kInquiry = 0x12;

    /// @brief SCSI operation code for GET CONFIGURATION
    inline constexpr uint8 kGetConfiguration = 0x46;

    /// @brief SCSI operation code for GET EVENT/STATUS NOTIFICATION
    inline constexpr uint8 kGetEventStatusNotification = 0x4A;

    /// @brief SCSI operation code for READ (10)
    inline constexpr uint8 kRead10 = 0x28;

    /// @brief SCSI operation code for READ CD
    inline constexpr uint8 kReadCD = 0xBE;

    /// @brief SCSI operation code for READ TOC
    inline constexpr uint8 kReadTOC = 0x43;

    /// @brief Build an INQUIRY command descriptor block without requesting vital product data.
    /// @param[in] length size of the output buffer
    /// @return an array with the command descriptor block for an INQUIRY command built from the given parameters
    inline std::array<uint8, 6> MakeInquiry(uint8 length) {
        std::array<uint8, 6> cdb{};
        cdb[0] = kInquiry;
        cdb[1] = 0x00; // EVPD=0, CmdDt=0
        cdb[2] = 0x00; // Page or operation code unused
        cdb[4] = length;
        return cdb;
    }

    /// @brief Build a GET CONFIGURATION command descriptor block requesting a single feature.
    /// @param[in] featureCode the feature to check for
    /// @param[in] length size of the output buffer
    /// @return an array with the command descriptor block for a GET CONFIGURATION command built from the given
    /// parameters
    inline std::array<uint8, 10> MakeGetConfigurationSingleFeature(uint16 featureCode, uint16 length) {
        std::array<uint8, 10> cdb{};
        cdb[0] = kGetConfiguration;
        cdb[1] = 0x02; // Request one feature
        cdb[2] = bit::extract<8, 15>(featureCode);
        cdb[3] = bit::extract<0, 7>(featureCode);
        cdb[7] = bit::extract<8, 15>(length);
        cdb[8] = bit::extract<0, 7>(length);
        return cdb;
    }

    /// @brief Build a GET EVENT/STATUS NOTIFICATION command descriptor block.
    /// @param[in] immed whether to poll immediately (`true`) or run asynchronously (`false`). Corresponds to the
    /// command's IMMED flag (bit 0 of byte 0).
    /// @param[in] classEvents bitmask of class events to be notified. Use the constants defined in the
    /// `ymir::scsi::notif_class` namespace.
    /// @param[in] length size of the output buffer
    /// @return an array with the command descriptor block for a GET EVENT/STATUS NOTIFICATION command built from the
    /// given parameters
    inline std::array<uint8, 10> MakeGetEventStatusNotification(bool immed, uint8 classEvents, uint16 length) {
        std::array<uint8, 10> cdb{};
        cdb[0] = kGetEventStatusNotification;
        cdb[1] = immed ? 1 : 0;               // bit 0 = IMMED
        cdb[4] = classEvents;                 // Media class events (bit 4)
        cdb[7] = bit::extract<8, 15>(length); // Allocation length (MSB)
        cdb[8] = bit::extract<0, 7>(length);  // Allocation length (LSB)
        return cdb;
    }

    /// @brief Build a READ (10) command descriptor block.
    /// @param[in] frameAddress the starting frame address (LBA) to read from
    /// @param[in] length the number of sectors to read
    /// @return an array with the command descriptor block for a READ (10) command built from the given parameters
    inline std::array<uint8, 10> MakeRead10(uint32 frameAddress, uint16 length) {
        // READ (10) CDB structure:
        // bytes  description
        //     0  Command operation code (=0x28)
        //     1  Flags
        //   2-5  Starting LBA
        //     6  Group number
        //   7-8  Transfer length in sectors
        //     9  Control byte
        std::array<uint8, 10> cdb{};
        cdb[0] = kReadCD;
        cdb[1] = 0x00;
        cdb[2] = bit::extract<24, 31>(frameAddress);
        cdb[3] = bit::extract<16, 23>(frameAddress);
        cdb[4] = bit::extract<8, 15>(frameAddress);
        cdb[5] = bit::extract<0, 7>(frameAddress);
        cdb[6] = 0x00;
        cdb[7] = bit::extract<8, 15>(length);
        cdb[8] = bit::extract<0, 7>(length);
        cdb[11] = 0x00;
        return cdb;
    }

    /// @brief Build a READ CD command descriptor block.
    /// @param[in] frameAddress the starting frame address (LBA) to read from
    /// @param[in] length the number of sectors to read
    /// @param[in] format expected sector type and format
    /// @param[in] subch subchannel selection
    /// @return an array with the command descriptor block for a READ CD command built from the given parameters
    inline std::array<uint8, 12> MakeReadCD(uint32 frameAddress, uint32 length, uint8 format, uint8 subch) {
        // READ CD CDB structure:
        // bytes  description
        //     0  Command operation code (=0xBE)
        //     1  Flags
        //          bits  description
        //             0  Relative addressing
        //             1  (reserved)
        //           2-4  Expected sector type
        //                  000 (0) = Any type
        //                  001 (1) = CDDA
        //                  010 (2) = Mode 1
        //                  011 (3) = Mode 2
        //                  100 (4) = Mode 2 Form 1
        //                  101 (5) = Mode 2 Form 2
        //                  110 (6) = (reserved)
        //                  111 (7) = (reserved)
        //           5-7  LUN
        //   2-5  Starting LBA
        //   6-8  Transfer length in sectors
        //     9  Expected sector type and format
        //          bits  description
        //             0  (reserved)
        //           1-2  Error flags
        //                  00 (0) = None
        //                  01 (1) = C2 Error Flag data
        //                  10 (2) = C2 and Block Error Flags
        //                  11 (3) = (reserved)
        //             3  EDC and ECC
        //             4  User data
        //           5-6  Header(s) code
        //                  00 (0) = None
        //                  01 (1) = Mode 1 or Mode 2 Form 1 4-byte header only
        //                  10 (2) = Mode 2 Form 1 or Form 2 subheader only
        //                  11 (3) = Header and subheader
        //             7  Sync field
        //    10  Subchannel selection (bits 0-2 only; others are reserved)
        //          000 (0) = None
        //          001 (1) = Raw subchannel data
        //          010 (2) = Q subchannel
        //          011 (3) = (reserved)
        //          100 (4) = R to W subchannels
        //          101 (5) = (reserved)
        //          110 (6) = (reserved)
        //          111 (7) = (reserved)
        std::array<uint8, 12> cdb{};
        cdb[0] = kReadCD;
        cdb[1] = 0x00;
        cdb[2] = bit::extract<24, 31>(frameAddress);
        cdb[3] = bit::extract<16, 23>(frameAddress);
        cdb[4] = bit::extract<8, 15>(frameAddress);
        cdb[5] = bit::extract<0, 7>(frameAddress);
        cdb[6] = bit::extract<16, 23>(length);
        cdb[7] = bit::extract<8, 15>(length);
        cdb[8] = bit::extract<0, 7>(length);
        cdb[9] = format;
        cdb[10] = subch;
        cdb[11] = 0x00;
        return cdb;
    }

    /// @brief Build a READ TOC command descriptor block which returns LBA addresses for all tracks in the disc.
    /// @param[in] length the size of the output buffer
    /// @return an array with the command descriptor block for a READ TOC command built from the given parameters
    inline std::array<uint8, 10> MakeReadTOC(uint32 length) {
        std::array<uint8, 10> cdb{};
        cdb[0] = kReadTOC;
        cdb[1] = 0x00; // Return LBA addresses
        cdb[2] = 0x00; // Standard TOC
        cdb[6] = 0x00; // Start from the first track
        cdb[7] = bit::extract<8, 15>(length);
        cdb[8] = bit::extract<0, 7>(length);
        cdb[9] = 0x00;
        return cdb;
    }

} // namespace op

/// @brief Feature codes for the GET CONFIGURATION command.
namespace features {

    inline constexpr uint16 kCDRead = 0x001E; ///< Supports the READ CD command

} // namespace features

/// @brief Notification class bits for the GET EVENT/STATUS NOTIFICATION command.
namespace notif_class {

    inline constexpr uint8 kMediaStatus = 1u << 4u; ///< Media Status Class Events

} // namespace notif_class

} // namespace ymir::scsi
