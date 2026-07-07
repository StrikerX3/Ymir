#pragma once

/**
@file
@brief Defines `ICDInterface`, an interface for physical, virtual or emulated CD devices.
*/

#include <ymir/core/types.hpp>

#include <span>

namespace ymir::media {

/// @brief Indicates the current drive and media state.
enum class DriveState {
    Unknown,      ///< State cannot be determined, typically because a physical device was disconnected
    TrayOpen,     ///< Tray open, no media
    NoDisc,       ///< Tray closed, no media
    MediaPresent, ///< Tray closed, media present
};

/// @brief Interface for CD device interfaces.
class ICDInterface {
public:
    virtual ~ICDInterface() = default;

    /// @brief Determines the current drive state.
    /// @return the current drive state
    virtual DriveState GetDriveState() const = 0;

    /// @brief Attempts to read a full raw 2352-byte sector at the specified frame address.
    /// @param[in] frameAddress the frame address (LBA) of the sector
    /// @param[in] out output buffer
    /// @return `true` if the sector was read successfully, `false` if not
    bool ReadSector(uint32 frameAddress, std::span<uint8, 2352> out);

protected:
    /// @brief Attempts to read a sector at the specified frame address.
    /// Implementations should prefer to read full raw sectors if possible, but may fall back to reading less data.
    /// If doing so, the read bytes must be placed in the correct location in the output buffer (e.g. if the
    /// implementation is only capable of reading the 2048-byte user data area, this chunk of data should be copied into
    /// the buffer starting at offset 0x10).
    /// `ReadSector(...)` synthesizes any missing data from the sector if this function returns less than 2352 bytes.
    ///
    /// @param[in] frameAddress the frame address (LBA) of the sector
    /// @param[in] out output buffer
    /// @return the number of bytes actually read
    virtual uint32 ReadSectorImpl(uint32 frameAddress, std::span<uint8, 2352> out) = 0;
};

} // namespace ymir::media
