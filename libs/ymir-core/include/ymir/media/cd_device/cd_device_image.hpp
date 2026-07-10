#pragma once

/**
@file
@brief Defines `ImageCDDevice`, a CD device that reads from a disc image contained in `ymir::media::Disc`.
*/

#include "cd_device_base.hpp"

#include <ymir/media/disc.hpp>

namespace ymir::media {

/// @brief Implements a CD device that reads from a disc image contained in an `ymir::media::Disc` instance.
class ImageCDDevice final : public ICDDevice {
public:
    ImageCDDevice(ymir::media::Disc &&disc);

    DriveState PollDriveState() const override;

    bool ReadPosition(uint32 frameAddress, DiscPosition &outPosition) override;

    [[nodiscard]] bool IsSeekDone() const override {
        // Always completes instantly
        return true;
    }

    [[nodiscard]] uint32 GetSeekFrameAddress() const override {
        return m_seekFAD;
    }

protected:
    std::vector<TOCEntry> ReadTOC() override;

    uint32 ReadSectorImpl(uint32 frameAddress, std::span<uint8, 2352> outSector) override;

    void BeginSeekToFrameAddressImpl(uint32 frameAddress) override;
    void BeginSeekToTrackIndexImpl(uint8 trackNumber, uint8 indexNumber) override;

private:
    ymir::media::Disc m_disc;
    uint32 m_seekFAD = 0xFFFFFF;
};

} // namespace ymir::media
