#include <ymir/media/cd_interface/cd_interface_host.hpp>

namespace ymir::media {

HostCDInterface::HostCDInterface(std::string path) {
    // TODO: try to open host device handle
    // if successful, start device thread to:
    // - periodically check drive state
    // - read and cache sectors
    // - update TOC, SaturnHeader and file system when a new disc is inserted
}

bool HostCDInterface::IsConnected() const {
    // TODO: return whether the device is open or not (check if handle is valid)
    // TODO: consider storing error message
    return false;
}

DriveState HostCDInterface::GetDriveState() const {
    // TODO: retrieve latest drive state
    return DriveState::NoDisc;
}

std::vector<TOCEntry> HostCDInterface::GetTOC() {
    // TODO: retrieve copy of cached TOC
    return {};
}

bool HostCDInterface::ReadPosition(uint32 frameAddress, DiscPosition &outPosition) {
    // TODO: read cached position for given FAD
    return false;
}

bool HostCDInterface::IsSeekDone() const {
    // TODO: check if async seek has completed
    return true;
}

uint32 HostCDInterface::GetSeekFrameAddress() const {
    // TODO: if async seek is done, get its target FAD, otherwise return 0xFFFFFF
    return 0xFFFFFF;
}

uint32 HostCDInterface::ReadSectorImpl(uint32 frameAddress, std::span<uint8, 2352> outSector) {
    // TODO: read cached sector
    // for now, block until read
    // later, change CDBlock/CDDrive to report "seek" or "busy" status until sector is available
    return 0;
}

void HostCDInterface::BeginSeekToFrameAddressImpl(uint32 frameAddress) {
    // TODO: request async seek to FAD
    // worker thread should also begin reading and caching sectors sequentially from that point on
}

void HostCDInterface::BeginSeekToTrackIndexImpl(uint8 trackNumber, uint8 indexNumber) {
    // TODO: request async seek to track:index
    // worker thread should also begin reading and caching sectors sequentially from that point on
}

} // namespace ymir::media
