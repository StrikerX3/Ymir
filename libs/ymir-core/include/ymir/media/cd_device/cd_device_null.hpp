#pragma once

/**
@file
@brief Defines `NullCDDevice`, a CD device that acts like an empty drive.
*/

#include "cd_device_base.hpp"

namespace ymir::media {

/// @brief Implements a null CD device that acts like an empty drive.
class NullCDDevice final : public ICDDevice {
public:
    DriveState GetDriveState() const override {
        return DriveState::NoDisc;
    }

    std::vector<TOCEntry> GetTOC() override {
        return {};
    }

    bool ReadPosition(uint32 frameAddress, DiscPosition &outPosition) override {
        return false;
    }

    [[nodiscard]] bool IsSeekDone() const override {
        return true;
    }

    [[nodiscard]] uint32 GetSeekFrameAddress() const override {
        return 0xFFFFFF;
    }

protected:
    uint32 ReadSectorImpl(uint32 frameAddress, std::span<uint8, 2352> out) override {
        return 0;
    }

    void BeginSeekToFrameAddressImpl(uint32 frameAddress) override {}

    void BeginSeekToTrackIndexImpl(uint8 trackNumber, uint8 indexNumber) override {}
};

} // namespace ymir::media
