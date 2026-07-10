#include <ymir/media/cd_interface/cd_interface_host.hpp>

namespace ymir::media {

DriveState HostCDInterface::GetDriveState() const {
    return DriveState::NoDisc;
}

std::vector<TOCEntry> HostCDInterface::GetTOC() {
    return {};
}

bool HostCDInterface::ReadPosition(uint32 frameAddress, DiscPosition &outPosition) {
    return false;
}

bool HostCDInterface::IsSeekDone() const {
    return true;
}

uint32 HostCDInterface::GetSeekFrameAddress() const {
    return 0xFFFFFF;
}

uint32 HostCDInterface::ReadSectorImpl(uint32 frameAddress, std::span<uint8, 2352> outSector) {
    return 0;
}

void HostCDInterface::BeginSeekToFrameAddressImpl(uint32 frameAddress) {}

void HostCDInterface::BeginSeekToTrackIndexImpl(uint8 trackNumber, uint8 indexNumber) {}

} // namespace ymir::media
