#include <ymir/media/cd_device/cd_device_image.hpp>

#include <ymir/util/arith_ops.hpp>

namespace ymir::media {

ImageCDDevice::ImageCDDevice(ymir::media::Disc &&disc)
    : m_disc(std::move(disc)) {
    m_header = m_disc.header;
    LoadTOC();
}

DriveState ImageCDDevice::PollDriveState() {
    if (m_disc.sessions.empty()) {
        return DriveState::NoDisc;
    }
    return DriveState::MediaPresent;
}

bool ImageCDDevice::ReadPosition(uint32 frameAddress, DiscPosition &outPosition) {
    if (m_disc.sessions.empty()) {
        return false;
    }

    const Session &session = m_disc.sessions.back();
    const uint8 trackIndex = session.FindTrackIndex(frameAddress);
    if (trackIndex == 0xFF) {
        return false;
    }

    const Track &track = session.tracks[trackIndex];
    const uint8 index = track.FindIndex(frameAddress);
    const sint32 relFAD = abs(static_cast<sint32>(frameAddress - track.index01FrameAddress));

    const auto [relM, relS, relF] = FADToMSF(relFAD);
    const auto [m, s, f] = FADToMSF(frameAddress);

    outPosition.controlADR = track.controlADR;
    outPosition.track = trackIndex + 1;
    outPosition.index = index == 0xFF ? 0x01 : index;
    outPosition.min = util::to_bcd(relM);
    outPosition.sec = util::to_bcd(relS);
    outPosition.frac = util::to_bcd(relF);
    outPosition.zero = 0;
    outPosition.amin = util::to_bcd(m);
    outPosition.asec = util::to_bcd(s);
    outPosition.afrac = util::to_bcd(f);

    return true;
}

std::vector<TOCEntry> ImageCDDevice::ReadTOC() {
    if (m_disc.sessions.empty()) {
        return {};
    }

    const Session &session = m_disc.sessions.back();
    const uint32 tocSize = session.tocSize;
    std::vector<TOCEntry> toc{tocSize};
    std::copy_n(session.toc.begin(), tocSize, toc.begin());
    return toc;
}

uint32 ImageCDDevice::ReadSectorImpl(uint32 frameAddress, std::span<uint8, 2352> out) {
    if (m_disc.sessions.empty()) {
        return 0;
    }

    const Session &session = m_disc.sessions.back();
    const Track *track = session.FindTrack(frameAddress);
    if (track == nullptr) {
        return 0;
    }
    if (track->ReadSector(frameAddress, out)) {
        // Swap endianness if necessary; audio tracks must be in big-endian
        if (track->controlADR == 0x01 && !track->bigEndian) {
            for (uint32 offset = 0; offset < 2352; offset += 2) {
                util::ByteSwap<uint16>(&out[offset]);
            }
        }
        return 2352;
    }

    return 0;
}

uint32 ImageCDDevice::ReadSectorUserDataImpl(uint32 frameAddress, std::span<uint8, 2048> out) {
    if (m_disc.sessions.empty()) {
        return 0;
    }

    const Session &session = m_disc.sessions.back();
    const Track *track = session.FindTrack(frameAddress);
    if (track == nullptr) {
        return 0;
    }
    if (track->controlADR == 0x01) {
        // Audio sectors should not be read with this method
        return 0;
    }
    if (track->ReadSectorUserData(frameAddress, out)) {
        return 2048;
    }

    return 0;
}

void ImageCDDevice::BeginSeekToFrameAddressImpl(uint32 frameAddress) {
    if (m_disc.sessions.empty()) {
        m_seekFAD = 0xFFFFFF;
    } else {
        m_seekFAD = frameAddress;
    }
}

void ImageCDDevice::BeginSeekToTrackIndexImpl(uint8 trackNumber, uint8 indexNumber) {
    if (m_disc.sessions.empty()) {
        m_seekFAD = 0xFFFFFF;
        return;
    }

    const auto &session = m_disc.sessions.back();
    if (trackNumber < session.firstTrackIndex + 1 || trackNumber > session.lastTrackIndex + 1) {
        m_seekFAD = 0xFFFFFF;
        return;
    }
    const auto &track = session.tracks[trackNumber - 1];
    if (indexNumber >= track.indices.size()) {
        m_seekFAD = 0xFFFFFF;
        return;
    }
    m_seekFAD = track.indices[indexNumber].startFrameAddress;
}

} // namespace ymir::media
