#include <ymir/media/device/cd_device_image.hpp>

namespace ymir::media {

ImageCDDevice::ImageCDDevice(Disc &&disc) {
    m_disc.Swap(std::move(disc));
}

size_t ImageCDDevice::ReadRawSectorImpl(uint32 frameAddress, std::span<uint8, 2352> out) {
    if (m_disc.sessions.empty()) {
        return 0;
    }

    frameAddress += 150;

    const auto &session = m_disc.sessions.back();
    const Track *track = session.FindTrack(frameAddress);
    if (track == nullptr) {
        return 0;
    }

    if (!track->ReadSector(frameAddress, out)) {
        return 0;
    }
    return 2352;
}

} // namespace ymir::media
