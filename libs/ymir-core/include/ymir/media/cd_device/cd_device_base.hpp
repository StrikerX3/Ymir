#pragma once

/**
@file
@brief Defines `ICDDevice`, an interface for physical, virtual or emulated CD devices.
*/

#include <ymir/core/types.hpp>

#include <ymir/media/cd_defs.hpp>
#include <ymir/media/filesystem.hpp>
#include <ymir/media/saturn_header.hpp>

#include <ymir/savestate/savestate_cd_interface.hpp>

#include <span>
#include <vector>

namespace ymir::media {

/// @brief Interface for CD reader devices.
class ICDDevice {
public:
    virtual ~ICDDevice() = default;

    /// @brief Updates the drive state, including the TOC and disc header information.
    /// If this returns `DriveState::MediaPresent`, the TOC and disc header are guaranteed to be updated.
    /// @return the current drive state
    DriveState PollDriveState();

    /// @brief Retrieves the current drive state since the last poll.
    /// @return the current drive state
    [[nodiscard]] DriveState GetDriveState() const {
        return m_driveState;
    }

    /// @brief Determines if a disc is present in the device.
    /// Convenient shorthand for `GetDriveState() == DriveState::MediaPresent`.
    /// @return `true` if there is a disc in the drive, `false` otherwise
    [[nodiscard]] bool HasDisc() const {
        return m_driveState == DriveState::MediaPresent;
    }

    /// @brief Retrieves the disc's table of contents.
    /// @return a reference to the disc's table of contents.
    [[nodiscard]] const TOC &GetTOC() const {
        return m_toc;
    }

    /// @brief Retrieves the Saturn disc header information.
    /// @return a reference to the Saturn disc header information.
    [[nodiscard]] const SaturnHeader &GetDiscHeader() const {
        return m_header;
    }

    /// @brief Retrieves the disc's filesystem structure.
    /// @return the disc's file system structure
    [[nodiscard]] const fs::Filesystem &GetFilesystem() const {
        return m_fs;
    }

    /// @brief Attempts to read a full raw 2352-byte sector at the specified frame address, optionally including
    /// position data. Audio track data is guaranteed to be in little-endian format.
    /// @param[in] frameAddress the frame address (LBA) of the sector
    /// @param[out] outSector sector data output buffer
    /// @param[out,opt] outPosition an optional pointer to receive position data
    /// @return `true` if the sector was read successfully, `false` if not
    bool ReadSector(uint32 frameAddress, std::span<uint8, 2352> outSector, DiscPosition *outPosition);

    /// @brief Attempts to read the 2048-byte user data area from sector at the specified frame address.
    /// Cannot be used to read audio track data.
    /// @param[in] frameAddress the frame address (LBA) of the sector
    /// @param[out] outSector sector data output buffer
    /// @return `true` if the sector was read successfully, `false` if not
    bool ReadSectorUserData(uint32 frameAddress, std::span<uint8, 2048> outSector);

    /// @brief Read position information (subcode Q data) from the specified sector.
    /// @param[in] frameAddress the frame address (LBA) of the sector
    /// @param[out] outPosition where to write position data into
    /// @return `true` if reading subcode Q data succeeded, `false` if failed
    virtual bool ReadPosition(uint32 frameAddress, DiscPosition &outPosition) = 0;

    /// @brief Requests the CD device to seek to the specified frame address.
    /// This operation is asynchronous. Use `IsSeekDone()` to check if the seek has completed and
    /// `GetSeekFrameAddress()` to retrieve the actual frame address reached by the device.
    /// @param[in] frameAddress the target frame address
    void BeginSeekToFrameAddress(uint32 frameAddress);

    /// @brief Requests the CD device to seek to the specified track:index.
    /// This operation is asynchronous. Use `IsSeekDone()` to check if the seek has completed and
    /// `GetSeekFrameAddress()` to retrieve the actual frame address reached by the device.
    /// @param[in] track the track number
    /// @param[in] index the index number
    void BeginSeekToTrackIndex(uint8 track, uint8 index);

    /// @brief Checks if a previous seek operation has completed.
    /// Always returns `true` if there is no disc in the drive.
    /// @return `true` if the last seek operation completed, `false` if in progress.
    [[nodiscard]] virtual bool IsSeekDone() const = 0;

    /// @brief Retrieves the target frame address of the last completed seek operation.
    /// Typically called after waiting until `IsSeekDone()` returns `true`.
    /// @return the frame address of the last completed seek operation.
    [[nodiscard]] virtual uint32 GetSeekFrameAddress() const = 0;

    /// @brief Sends a hint to the device to stop reading the disc.
    virtual void HintStop() {}

    // -------------------------------------------------------------------------
    // Save states
    //
    // NOTE: Save states are implemented in terms of `IsSeekDone()` and `GetSeekFrameAddress()`. It avoids reexecuting
    // seeks to minimize strain on real hardware.

    void SaveState(savestate::CDInterfaceSaveState &state) const;
    [[nodiscard]] bool ValidateState(const savestate::CDInterfaceSaveState &state) const;
    void LoadState(const savestate::CDInterfaceSaveState &state);

protected:
    /// @brief Drive state as of the latest poll.
    DriveState m_driveState = DriveState::Unknown;

    /// @brief The parsed Saturn disc header block.
    /// Updated when the device is initialized and during `PollDriveState()`.
    SaturnHeader m_header;

    /// @brief The disc's table of contents.
    /// Updated when the device is initialized and during `PollDriveState()`.
    TOC m_toc;

    /// @brief The disc's file system.
    /// Updated when the device is initialized and during `PollDriveState()`.
    fs::Filesystem m_fs;

    /// @brief Updates the drive state, including the TOC and disc header information.
    /// The TOC and disc header must be updated when returning `DriveState::MediaPresent`.
    /// @return the current drive state
    virtual DriveState PollDriveStateImpl() = 0;

    /// @brief Attempts to read a raw sector at the specified frame address.
    /// Implementations should prefer to read full raw sectors if possible, but may fall back to reading less data.
    /// If doing so, the read bytes must be placed in the correct location in the output buffer (e.g. if the
    /// implementation is only capable of reading the 2048-byte user data area, this chunk of data should be copied into
    /// the buffer starting at offset 0x10).
    /// `ReadSector(...)` synthesizes any missing data from the sector if this function returns less than 2352 bytes.
    /// Audio track data must be in little-endian format.
    /// If the position pointer is provided, it must be filled in along with sector data (all-or-nothing).
    ///
    /// @param[in] frameAddress the frame address (LBA) of the sector
    /// @param[out] outSector sector data output buffer
    /// @param[out,opt] outPosition an optional pointer to receive position data
    /// @return the number of bytes actually read
    virtual uint32 ReadSectorImpl(uint32 frameAddress, std::span<uint8, 2352> outSector, DiscPosition *outPosition) = 0;

    /// @brief Attempts to read the user data area of a sector at the specified frame address.
    /// Should fail if attempting to read an audio track.
    ///
    /// @param[in] frameAddress the frame address (LBA) of the sector
    /// @param[out] outSector sector data output buffer
    /// @return the number of bytes actually read
    virtual uint32 ReadSectorUserDataImpl(uint32 frameAddress, std::span<uint8, 2048> outSector) = 0;

    /// @brief Requests the CD device to seek to the specified frame address.
    /// This operation is asynchronous. Use `IsSeekDone()` to check if the seek has completed and
    /// `GetSeekFrameAddress()` to retrieve the actual frame address reached by the device.
    /// If multiple requests are sent back-to-back before previous requests are complete, only the last one must be
    /// honored. Repeated requests with the same parameters should be ignored.
    /// @param[in] frameAddress the target frame address
    virtual void BeginSeekToFrameAddressImpl(uint32 frameAddress) = 0;

    /// @brief Requests the CD device to seek to the specified track:index.
    /// This operation is asynchronous. Use `IsSeekDone()` to check if the seek has completed and
    /// `GetSeekFrameAddress()` to retrieve the actual frame address reached by the device.
    /// If multiple requests are sent back-to-back before previous requests are complete, only the last one must be
    /// honored. Repeated requests with the same parameters should be ignored.
    /// @param[in] track the track number
    /// @param[in] index the index number
    virtual void BeginSeekToTrackIndexImpl(uint8 track, uint8 index) = 0;

    /// @brief Reconciles the seek state of the given save state and the currently loaded device.
    /// The underlying device may be requested to seek to a new address in order to match the stored state.
    /// @param[in] state the save state
    void ReconcileSeekState(const savestate::CDInterfaceSaveState &state);

private:
    /// @brief The last seek target specified by a `BeginSeek*` command.
    /// If bit 31 is set, the target specifies a track number in bits 8-15 and index number in bits 0-7.
    /// Otherwise, bits 0-23 specify a frame address.
    uint32 m_seekTarget;
};

} // namespace ymir::media
