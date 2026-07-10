#include <ymir/media/cd_interface/cd_interface_host.hpp>

namespace ymir::media {

HostCDInterface::HostCDInterface(std::string path) {
    m_devHandle = host::OpenCDDrive(path);
    if (m_devHandle == host::kInvalidDeviceHandle) {
        return;
    }

    // TODO: start device worker thread:
    // - periodically check drive state
    // - read and cache sectors
    // - update TOC, SaturnHeader and file system when a new disc is inserted
}

HostCDInterface::~HostCDInterface() {
    // TODO: stop device worker thread if running
    if (m_devHandle != host::kInvalidDeviceHandle) {
        host::CloseDeviceHandle(m_devHandle);
    }
}

bool HostCDInterface::IsConnected() const {
    return m_devHandle != host::kInvalidDeviceHandle;
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
    // cancel any ongoing seek operations, override any pending seek operations
}

void HostCDInterface::BeginSeekToTrackIndexImpl(uint8 trackNumber, uint8 indexNumber) {
    // TODO: request async seek to track:index
    // worker thread should also begin reading and caching sectors sequentially from that point on
    // cancel any ongoing seek operations, override any pending seek operations
}

} // namespace ymir::media
