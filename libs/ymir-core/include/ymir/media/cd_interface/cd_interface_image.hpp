#pragma once

/**
@file
@brief Defines `ImageCDInterface`, a CD interface that reads from a disc image contained in `ymir::media::Disc`.
*/

#include "cd_interface_base.hpp"

#include <ymir/media/disc.hpp>

namespace ymir::media {

/// @brief Implements a CD interface that reads from a disc image contained in an `ymir::media::Disc` instance.
class ImageCDInterface final : public ICDInterface {
public:
    ImageCDInterface(ymir::media::Disc &&disc);

    DriveState GetDriveState() const override;

protected:
    uint32 ReadSectorImpl(uint32 frameAddress, std::span<uint8, 2352> out) override;

private:
    ymir::media::Disc m_disc;
};

} // namespace ymir::media
