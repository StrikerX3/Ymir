#pragma once

/**
@file
@brief Defines `HostCDInterface`, a CD interface that connects to a physical CD drive on the host.
See also @ref ymir/media/host_cd.hpp.
*/

#include "cd_interface_base.hpp"

#include <ymir/media/host_cd.hpp>

namespace ymir::media {

/// @brief Implements a host CD interface that connects to a physical CD drive on the host.
class HostCDInterface final : public ICDInterface {
public:
    /// @brief Creates a host CD device from the specified path.
    /// @param[in] path the native device path. Accepted path formats vary per operating system:
    /// - Windows: drive letters ("D:"), NT device paths ("\Device\CdRom0") or DOS paths ("\\.\D:", "\\.\CdRom0")
    /// - Linux: SCSI generic device paths ("/dev/sg0")
    /// - Other systems: TBD
    HostCDInterface(std::string path);

    ~HostCDInterface();

    /// @brief Checks if the device was successfully connected.
    /// @return `true` if connected successfully, `false` if not
    bool IsConnected() const;

    DriveState GetDriveState() const override;

    std::vector<TOCEntry> GetTOC() override;

    bool ReadPosition(uint32 frameAddress, DiscPosition &outPosition) override;

    [[nodiscard]] bool IsSeekDone() const override;
    [[nodiscard]] uint32 GetSeekFrameAddress() const override;

protected:
    uint32 ReadSectorImpl(uint32 frameAddress, std::span<uint8, 2352> out) override;

    void BeginSeekToFrameAddressImpl(uint32 frameAddress) override;
    void BeginSeekToTrackIndexImpl(uint8 trackNumber, uint8 indexNumber) override;

private:
    host::DeviceHandle m_devHandle = host::kInvalidDeviceHandle;
};

} // namespace ymir::media
