#include <ymir/media/cd_device/cd_device_host.hpp>

namespace ymir::media {

HostCDDevice::HostCDDevice(std::string path, const CBOnMediaChanged &cbOnMediaChanged)
    : m_cbOnMediaChanged(cbOnMediaChanged) {
    m_devHandle = host::OpenCDDrive(path);
    if (m_devHandle == host::kInvalidDeviceHandle) {
        return;
    }

    // Async design:
    // - TOC and SaturnHeader (collectively "disc info") are protected by a mutex
    // - Worker thread keeps its own copy of the drive state as well as the TOC and SaturnHeader
    // - An atomic flag is set whenever media is changed, either by detecting a drive state change during the polling
    //   loop or through a notification
    // - PollDriveState() updates the drive state and copies over the TOC and SaturnHeader if changed.
    //   - If the media presence changed or the dedicated flag is set, the OnMediaChanged callback is invoked.
    // - PollDriveState() is invoked as part of the emulation loop. If the emulator is paused, any state changes will be
    //   held pending until emulation resumes
    // - The frontend may register a callback to receive immediate notifications on device state changes

    // TODO: host should start observing device events
    // - if media changed, mark disc info as dirty
    // TODO: start device worker thread:
    // - periodically check drive state; mark as dirty if media changed
    // - read and cache sectors if requested
    // - read TOC and SaturnHeader when a new disc is inserted
    //   - once complete, mark as ready so that the PollDriveState() knows when to invoke the callback
    //   - IMPORTANT: drive state MUST NOT be changed to MediaPresent while the worker thread is still reading these!
    // TODO: move this block to the worker thread
    {
        std::array<uint8, 2352> headerSector{};
        if (ReadSector(0, headerSector)) {
            m_header.ReadFrom(headerSector);
        } else {
            m_header.Invalidate();
        }
        LoadTOC();
    }
}

HostCDDevice::~HostCDDevice() {
    // TODO: stop device worker thread if running
    // TODO: cleanup other resources (notification handles, etc.)
    if (m_devHandle != host::kInvalidDeviceHandle) {
        host::CloseDeviceHandle(m_devHandle);
    }
}

bool HostCDDevice::IsConnected() const {
    return m_devHandle != host::kInvalidDeviceHandle;
}

DriveState HostCDDevice::PollDriveState() {
    // TODO: retrieve latest drive state
    // - if media presence has changed, clear TOC and header
    //   - if media is now present, dispatch TOC+header read
    return DriveState::NoDisc;
}

bool HostCDDevice::ReadPosition(uint32 frameAddress, DiscPosition &outPosition) {
    // TODO: read cached position for given FAD
    return false;
}

bool HostCDDevice::IsSeekDone() const {
    // TODO: check if async seek has completed
    return true;
}

uint32 HostCDDevice::GetSeekFrameAddress() const {
    // TODO: if async seek is done, get its target FAD, otherwise return 0xFFFFFF
    return 0xFFFFFF;
}

std::vector<TOCEntry> HostCDDevice::ReadTOC() {
    // TODO: retrieve copy of cached TOC
    return {};
}

uint32 HostCDDevice::ReadSectorImpl(uint32 frameAddress, std::span<uint8, 2352> outSector) {
    // TODO: read cached raw sector
    // for now, block until read
    // later, change CDBlock/CDDrive to report "seek" or "busy" status until sector is available
    return 0;
}

uint32 HostCDDevice::ReadSectorUserDataImpl(uint32 frameAddress, std::span<uint8, 2048> outSector) {
    // TODO: read cached raw sector, extract user data area only
    // for now, block until read
    // later, change CDBlock/CDDrive to report "seek" or "busy" status until sector is available
    return 0;
}

void HostCDDevice::BeginSeekToFrameAddressImpl(uint32 frameAddress) {
    // TODO: request async seek to FAD
    // worker thread should also begin reading and caching sectors sequentially from that point on.
    // cancel any ongoing seek operations, override any pending seek operations
}

void HostCDDevice::BeginSeekToTrackIndexImpl(uint8 trackNumber, uint8 indexNumber) {
    // TODO: request async seek to track:index
    // worker thread should also begin reading and caching sectors sequentially from that point on.
    // cancel any ongoing seek operations, override any pending seek operations
}

} // namespace ymir::media
