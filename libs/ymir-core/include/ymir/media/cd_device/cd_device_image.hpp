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

    bool ReadPosition(uint32 frameAddress, DiscPosition &outPosition) override;

    [[nodiscard]] bool IsSeekDone() const override {
        // Always completes instantly
        return true;
    }

    [[nodiscard]] uint32 GetSeekFrameAddress() const override {
        return m_seekFAD;
    }

protected:
    DriveState PollDriveStateImpl() override;

    uint32 ReadSectorImpl(uint32 frameAddress, std::span<uint8, 2352> out, DiscPosition *outPosition) override;
    uint32 ReadSectorUserDataImpl(uint32 frameAddress, std::span<uint8, 2048> out) override;

    void BeginSeekToFrameAddressImpl(uint32 frameAddress) override;
    void BeginSeekToTrackIndexImpl(uint8 trackNumber, uint8 indexNumber) override;

private:
    ymir::media::Disc m_disc;
    uint32 m_seekFAD = 0xFFFFFF;

    std::vector<TOCEntry> ReadTOC();

    struct FilesystemReader : fs::IFilesystemCDReader {
        FilesystemReader(ImageCDDevice &dev)
            : m_dev(dev) {}

        [[nodiscard]] bool HasDisc() const override;
        [[nodiscard]] const TOC &GetTOC() const override;
        [[nodiscard]] const SaturnHeader &GetDiscHeader() const override;
        bool ReadSectorUserData(uint32 frameAddress, std::span<uint8, 2048> outSector) override;

    private:
        ImageCDDevice &m_dev;
    } m_fsReader{*this};
};

} // namespace ymir::media
