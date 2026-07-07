#pragma once

/**
@file
@brief Defines `NullCDInterface`, a CD interface that always acts like an empty drive.
*/

#include "cd_interface_base.hpp"

namespace ymir::media {

/// @brief Implements a null CD interface that acts like an always empty disc.
class NullCDInterface final : public ICDInterface {
public:
    DriveState GetDriveState() const override {
        return DriveState::NoDisc;
    }

protected:
    uint32 ReadSectorImpl(uint32 frameAddress, std::span<uint8, 2352> out) override {
        return 0;
    }
};

} // namespace ymir::media
