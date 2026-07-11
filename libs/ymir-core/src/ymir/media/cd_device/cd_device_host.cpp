#include <ymir/media/cd_device/cd_device_host.hpp>

#include <chrono>

namespace ymir::media {

// Async design notes:
// - TOC and SaturnHeader (collectively "disc info") are protected by a mutex
// - Worker thread keeps its own copy of the drive state as well as the TOC and SaturnHeader
// - An atomic flag is set whenever media is changed, either by detecting a drive state change during the polling loop
//   or through a notification
// - Another atomic flag is set once the TOC and SaturnHeader finish updating
// - PollDriveState() updates the drive state and copies over the TOC and SaturnHeader if changed.
//   - If the media presence changed or the dedicated flag is set, the OnMediaChanged callback is invoked.
// - PollDriveState() is invoked as part of the emulation loop. If the emulator is paused, any state changes will be
//   held pending until emulation resumes
// - The frontend may register a callback to receive immediate notifications on device state changes

HostCDDevice::HostCDDevice(std::string path, const CBOnMediaChanged &cbOnMediaChanged)
    : m_cbOnMediaChanged(cbOnMediaChanged) {
    m_devHandle = host::OpenCDDrive(path);
    if (m_devHandle == host::kInvalidDeviceHandle) {
        return;
    }

    m_workerThread = std::thread{[this] { WorkerThread(); }};

    // TODO: host should start observing device events
    // - if media changed, mark disc info as dirty
}

HostCDDevice::~HostCDDevice() {
    if (m_workerThread.joinable()) {
        EnqueueCommand(Command::Quit());
        m_workerThread.join();
    }
    // TODO: cleanup other resources (notification handles, etc.)
    if (m_devHandle != host::kInvalidDeviceHandle) {
        host::CloseDeviceHandle(m_devHandle);
    }
}

bool HostCDDevice::IsConnected() const {
    return m_devHandle != host::kInvalidDeviceHandle;
}

DriveState HostCDDevice::PollDriveState() {
    auto &ts = m_threadState;

    if (ts.discInfoChanged.load(std::memory_order_acquire)) {
        std::unique_lock lock{ts.mtxDiscInfo};

        m_toc = ts.toc;
        m_header = ts.header;
        ts.discInfoChanged.store(false, std::memory_order_release);
    }

    bool notifyMediaStateChange = false;
    const DriveState newDriveState = ts.driveState;
    if (m_driveState != newDriveState) {
        if (m_driveState == DriveState::MediaPresent || newDriveState == DriveState::MediaPresent) {
            notifyMediaStateChange = true;
        }
        m_driveState = newDriveState;
    }

    bool expected = true;
    notifyMediaStateChange |= ts.mediaStateChanged.compare_exchange_strong(expected, false);
    if (notifyMediaStateChange) {
        m_cbOnMediaChanged();
    }

    return m_driveState;
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

void HostCDDevice::EnqueueCommand(Command &&cmd) {
    m_workQueue.enqueue(m_ptokWorkQueue, cmd);
}

void HostCDDevice::WorkerThread() {
    using namespace std::chrono_literals;

    auto &ts = m_threadState;

    ReadHeaderAndTOC();

    Command cmd{};
    bool running = true;
    while (running) {
        // TODO: read and cache sectors if requested
        if (m_workQueue.wait_dequeue_timed(m_ctokWorkQueue, cmd, 1s)) {
            using CmdType = Command::Type;
            switch (cmd.type) {
            case CmdType::Quit: running = false; break;
            }
        } else {
            // TODO: periodically check drive state; mark as dirty in `ts` if media changed
            // TODO: read TOC and SaturnHeader when a new disc is inserted
            // - once complete, mark as ready so that the PollDriveState() knows when to invoke the callback
            // - IMPORTANT: drive state MUST NOT be changed to MediaPresent while the worker thread is still reading
            //   these!

            // NOTE: This must happen *after* the TOC and SaturnHeader are read.
            // PollDriveState() contract requires them to be updated when returning `DriveState::MediaPresent`.
            ts.mediaStateChanged.store(true, std::memory_order_release);
        }
    }
}

void HostCDDevice::ReadHeaderAndTOC() {
    auto &ts = m_threadState;

    std::unique_lock lock{ts.mtxDiscInfo};

    std::array<uint8, 2352> headerSector{};
    if (ReadSector(0, headerSector)) {
        ts.header.ReadFrom(headerSector);
    } else {
        ts.header.Invalidate();
    }
    ts.toc.LoadFrom(ReadTOC());
    ts.discInfoChanged.store(true, std::memory_order_release);
}

} // namespace ymir::media
