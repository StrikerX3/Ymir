#pragma once

/**
@file
@brief Defines `CDInterface`, an object that holds a CD interface implementation.
*/

#include "cd_device/cd_device_base.hpp"

#include "cd_defs.hpp"
#include "cd_interface_callbacks.hpp"
#include "disc.hpp"

#include <ymir/savestate/savestate_cd_interface.hpp>

#include <memory>

namespace ymir::media {

/// @brief Holds and manages a CD interface.
class CDInterface {
public:
    CDInterface();

    /// @brief Maps callbacks for certain events.
    /// Used internally by the emulator core. Should not be used externally.
    /// @param[in] cbOnMediaChanged the callback invoked when the media is changed
    void MapCallbacks(CBOnMediaChanged cbOnMediaChanged) {
        m_cbOnMediaChanged = cbOnMediaChanged;
    }

    /// @brief Loads a disc image.
    /// @param[in] disc the disc image to load
    void LoadDisc(Disc &&disc);

    /// @brief Attempts to open a host CD device at the specified path.
    /// See @ref ymir::media::HostCDDevice::HostCDDevice(std::string) for details on what path formats are /// accepeted
    /// for each supported operating system.
    /// @param[in] path the host device path. Enumerate with `ymir::media::host::EnumerateHostCDDrives()`.
    /// @return `true` if the device was successfully opened, `false` otherwise
    bool OpenHostDevice(std::string path);

    /// @brief Ejects the disc.
    void Eject();

    /// @brief Updates the drive state, including the TOC and disc header information.
    /// If this returns `DriveState::MediaPresent`, the TOC and disc header are guaranteed to be updated.
    /// @return the current drive state
    DriveState PollDriveState() const;

    /// @brief Retrieves the current drive state since the last poll.
    /// @return the current drive state
    [[nodiscard]] DriveState GetDriveState() const;

    /// @brief Determines if a disc is present in the device.
    /// @return `true` if there is a disc in the drive, `false` otherwise
    [[nodiscard]] bool HasDisc() const;

    /// @brief Retrieves the disc's table of contents.
    /// @return a reference to the disc's TOC. Empty if no disc is loaded.
    [[nodiscard]] const TOC &GetTOC() const;

    /// @brief Retrieves the Saturn disc header information.
    /// @return a reference to the Saturn disc header information.
    [[nodiscard]] const SaturnHeader &GetDiscHeader() const;

    /// @brief Retrieves the disc's filesystem structure.
    /// @return the disc's file system structure
    [[nodiscard]] const fs::Filesystem &GetFilesystem() const;

    /// @brief Reads a raw sector from the disc.
    /// @param[in] frameAddress the frame address to read
    /// @param[out] outSector the output buffer to read the sector into
    /// @return `true` if the sector was read successfully, `false` if frame address is out of range, there is no disc,
    /// or an error occurred
    bool ReadSector(uint32 frameAddress, std::span<uint8, 2352> outSector);

    /// @brief Reads the user data area of a sector from the disc.
    /// @param[in] frameAddress the frame address to read
    /// @param[out] outSector the output buffer to read the sector into
    /// @return `true` if the sector was read successfully, `false` if frame address is out of range, there is no disc,
    /// or an error occurred
    bool ReadSectorUserData(uint32 frameAddress, std::span<uint8, 2048> outSector);

    /// @brief Reads position information (subcode Q data) from the specified sector.
    /// @param[in] frameAddress the frame address (LBA) of the sector
    /// @param[out] outPosition where to write position data into
    /// @return `true` if reading subcode Q data succeeded, `false` if failed
    bool ReadPosition(uint32 frameAddress, DiscPosition &outPosition);

    /// @brief Requests the CD device to seek to the specified frame address.
    /// This operation is asynchronous. Use `IsSeekDone()` to check if the seek has completed and
    /// `GetSeekFrameAddress()` to retrieve the actual frame address reached by the device.
    /// @param[in] frameAddress the target frame address
    void BeginSeekToFrameAddress(uint32 frameAddress);

    /// @brief Requests the CD device to seek to the specified track:index.
    /// This operation is asynchronous. Use `IsSeekDone()` to check if the seek has completed and
    /// `GetSeekFrameAddress()` to retrieve the actual frame address reached by the device.
    /// @param[in] trackNumber the track number
    /// @param[in] indexNumber the index number
    void BeginSeekToTrackIndex(uint8 trackNumber, uint8 indexNumber);

    /// @brief Checks if a previous seek operation has completed.
    /// @return `true` if the last seek operation completed, `false` if in progress.
    [[nodiscard]] bool IsSeekDone() const;

    /// @brief Retrieves the target frame address of the last completed seek operation.
    /// Typically called after waiting until `IsSeekDone()` returns `true`.
    /// @return the frame address of the last completed seek operation.
    [[nodiscard]] uint32 GetSeekFrameAddress() const;

    // -------------------------------------------------------------------------
    // Save states

    void SaveState(savestate::CDInterfaceSaveState &state) const;
    [[nodiscard]] bool ValidateState(const savestate::CDInterfaceSaveState &state) const;
    void LoadState(const savestate::CDInterfaceSaveState &state);

private:
    std::unique_ptr<ICDDevice> m_cdDevice;
    CBOnMediaChanged m_cbOnMediaChanged;
};

} // namespace ymir::media
