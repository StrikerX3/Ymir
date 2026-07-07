#include <ymir/media/cd_interface/cd_interface_image.hpp>

namespace ymir::media {

ImageCDInterface::ImageCDInterface(ymir::media::Disc &&disc)
    : m_disc(std::move(disc)) {}

DriveState ImageCDInterface::GetDriveState() const {
    if (m_disc.sessions.empty()) {
        return DriveState::NoDisc;
    }
    return DriveState::MediaPresent;
}

uint32 ImageCDInterface::ReadSectorImpl(uint32 frameAddress, std::span<uint8, 2352> out) {
    if (m_disc.sessions.empty()) {
        return 0;
    }

    const Session &session = m_disc.sessions.back();
    const Track *track = session.FindTrack(frameAddress);
    if (track == nullptr) {
        return 0;
    }
    if (track->ReadSector(frameAddress, out)) {
        return 2352;
    }

    return 0;
}

} // namespace ymir::media
