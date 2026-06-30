#pragma once

/**
@file
@brief Defines `ImageCDDevice`, a CD device that reads from a disc image.
*/

#include "cd_device_base.hpp"

#include <ymir/media/disc.hpp>

namespace ymir::media {

/// @brief A CD device that reads from a disc image.
class ImageCDDevice final : public ICDDevice {
public:
    ImageCDDevice(Disc &&disc);

protected:
    // -------------------------------------------------------------------------
    // ICDDevice implementation

    size_t ReadRawSectorImpl(uint32 frameAddress, std::span<uint8, 2352> out) override;

private:
    Disc m_disc;
};

} // namespace ymir::media
