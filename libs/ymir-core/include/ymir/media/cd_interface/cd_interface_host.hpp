#pragma once

/**
@file
@brief Defines `HostCDInterface`, a CD interface that connects to a physical CD drive on the host.
See also @ref ymir/media/host_cd.hpp.
*/

#include "cd_interface_base.hpp"

namespace ymir::media {

/// @brief Implements a host CD interface that connects to a physical CD drive on the host.
class HostCDInterface final : public ICDInterface {
public:
    DriveState GetDriveState() const override;

    std::vector<TOCEntry> GetTOC() override;

    bool ReadPosition(uint32 frameAddress, DiscPosition &outPosition) override;

    [[nodiscard]] bool IsSeekDone() const override;
    [[nodiscard]] uint32 GetSeekFrameAddress() const override;

protected:
    uint32 ReadSectorImpl(uint32 frameAddress, std::span<uint8, 2352> out) override;

    void BeginSeekToFrameAddressImpl(uint32 frameAddress) override;
    void BeginSeekToTrackIndexImpl(uint8 trackNumber, uint8 indexNumber) override;
};

} // namespace ymir::media
