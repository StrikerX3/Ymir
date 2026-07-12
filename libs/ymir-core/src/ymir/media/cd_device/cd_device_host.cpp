#include <ymir/media/cd_device/cd_device_host.hpp>

#include <ymir/media/scsi.hpp>

#include <ymir/util/arith_ops.hpp>
#include <ymir/util/dev_log.hpp>
#include <ymir/util/thread_name.hpp>

#include <chrono>

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

namespace ymir::media {

namespace grp {

    // -----------------------------------------------------------------------------
    // Dev log groups

    // Hierarchy:
    //
    // host

    struct host {
        static constexpr bool enabled = true;
        static constexpr devlog::Level level = devlog::level::debug;
        static constexpr std::string_view name = "CDDev-Host";
    };

} // namespace grp

HostCDDevice::HostCDDevice(std::string path, const CBOnMediaChanged &cbOnMediaChanged)
    : m_cbOnMediaChanged(cbOnMediaChanged)
    , m_path(path) {
    m_devHandle = host::OpenCDDrive(path);
    if (m_devHandle == host::kInvalidDeviceHandle) {
        return;
    }

    // TODO: subscribe to notifications for device and media state changes
    // - if media changed, mark disc info as dirty
    // - notify drive removal somehow

    m_workerThread = std::thread{[this] { WorkerThread(); }};
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

bool HostCDDevice::ReadPosition(uint32 frameAddress, DiscPosition &outPosition) {
    auto entry = TryGetCachedSector(frameAddress);
    if (entry == nullptr) {
        return false;
    }

    outPosition = entry->pos;
    return true;
}

bool HostCDDevice::IsSeekDone() const {
    return m_seekState.committedCount == m_seekState.requestedCount;
}

uint32 HostCDDevice::GetSeekFrameAddress() const {
    return m_seekState.frameAddress;
}

DriveState HostCDDevice::PollDriveStateImpl() {
    auto &ts = m_threadState;

    if (ts.discInfoChanged.load(std::memory_order_acquire)) {
        std::unique_lock lock{ts.mtxDiscInfo};

        m_toc = ts.toc;
        m_header = ts.header;
        m_fs = ts.fs;
        ts.discInfoChanged.store(false, std::memory_order_release);
    }

    bool notifyMediaStateChange = false;
    const DriveState newDriveState = ts.targetDriveState;
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

    m_seekState.frameAddress = ts.seekFAD.load(std::memory_order_acquire);
    m_seekState.committedCount = ts.seekCounter.load(std::memory_order_acquire);

    return m_driveState;
}

uint32 HostCDDevice::ReadSectorImpl(uint32 frameAddress, std::span<uint8, 2352> outSector, DiscPosition *outPosition) {
    auto entry = TryGetCachedSector(frameAddress);
    if (entry == nullptr) {
        return 0u;
    }

    std::copy_n(entry->sector.begin(), outSector.size(), outSector.begin());
    if (outPosition != nullptr) {
        *outPosition = entry->pos;
    }
    return outSector.size();
}

uint32 HostCDDevice::ReadSectorUserDataImpl(uint32 frameAddress, std::span<uint8, 2048> outSector) {
    auto entry = TryGetCachedSector(frameAddress);
    if (entry == nullptr) {
        return 0u;
    }

    std::copy_n(entry->sector.begin() + 0x10, outSector.size(), outSector.begin());
    return outSector.size();
}

void HostCDDevice::BeginSeekToFrameAddressImpl(uint32 frameAddress) {
    const uint32 target = bit::extract<0, 23>(frameAddress);
    if (m_seekState.requestedTarget != 0xFFFFFFFF && m_seekState.requestedTarget == target) {
        // Same request as before, no need to strain the hardware with this
        return;
    }
    m_seekState.requestedTarget = target;
    ++m_seekState.requestedCount;
    EnqueueCommand(Command::SeekFrameAddress(m_seekState.requestedCount, frameAddress));
}

void HostCDDevice::BeginSeekToTrackIndexImpl(uint8 track, uint8 index) {
    const uint32 target = (1u << 31u) | (track << 8u) | index;
    if (m_seekState.requestedTarget != 0xFFFFFFFF && m_seekState.requestedTarget == target) {
        // Same request as before, no need to strain the hardware with this
        return;
    }
    m_seekState.requestedTarget = target;
    ++m_seekState.requestedCount;
    EnqueueCommand(Command::SeekTrackIndex(m_seekState.requestedCount, track, index));
}

void HostCDDevice::EnqueueCommand(Command &&cmd) {
    m_workQueue.enqueue(m_ptokWorkQueue, cmd);
}

void HostCDDevice::WorkerThread() {
    util::SetCurrentThreadName(fmt::format("{} worker", m_path).c_str());

    using namespace std::chrono_literals;

    auto &ts = m_threadState;

    ts.driveState = host::PollDriveState(m_devHandle);
    InitializeDiscInfo();

    Command cmd{};
    bool running = true;
    while (running) {
        // TODO: make this configurable
        static constexpr uint32 kMaxSectorFetchSize = 16;
        const uint32 sectorFetchSize = m_sectorCache.GetCapacity() - m_sectorCache.Size();

        const bool shouldReadSectors =
            // actively reading sectors
            ts.readSectors &&
            // there's enough room in cache
            sectorFetchSize > 0;

        const bool dequeued = shouldReadSectors ? m_workQueue.try_dequeue(m_ctokWorkQueue, cmd)
                                                : m_workQueue.wait_dequeue_timed(m_ctokWorkQueue, cmd, 1s);

        if (dequeued) {
            using CmdType = Command::Type;
            switch (cmd.type) {
            case CmdType::SeekFrameAddress: //
            {
                // No need to issue SEEK command to host device for this; the frame address is (obviously) known

                // Constraint to valid disc range (including lead-out area)
                const uint32 frameAddress =
                    std::clamp(cmd.data.seek.target.frameAddress, 150u, m_toc.GetLeadOutFrameAddress());
                SetSeekResult(cmd.data.seek.counter, frameAddress);
                break;
            }
            case CmdType::SeekTrackIndex: //
            {
                const TrackInfo *trackInfo = m_toc.GetTrackInfoForNumber(cmd.data.seek.target.track);
                if (trackInfo != nullptr) {
                    if (cmd.data.seek.target.index == 1) {
                        // Trivial case: seek to start of track; target frame address is known from TOC
                        SetSeekResult(cmd.data.seek.counter, trackInfo->startFrameAddress);
                    } else {
                        const uint8 track = cmd.data.seek.target.track;
                        const uint8 index = cmd.data.seek.target.index;

                        if (auto it = ts.indexFADs.find({track, index}); it != ts.indexFADs.end()) {
                            // Retrieve cached result
                            SetSeekResult(cmd.data.seek.counter, it->second);
                        } else {
                            // Binary search for index between the track's start and end FADs.
                            // Include start of next track in upper bound in case the index is out of range.
                            uint32 lbFAD = trackInfo->startFrameAddress;
                            uint32 ubFAD = trackInfo->endFrameAddress + 1u;
                            uint32 frameAddress = (lbFAD + ubFAD) >> 1u;

                            std::array<uint8, 2352> sectorBuffer{};
                            DiscPosition pos{};
                            while (lbFAD != ubFAD) {
                                if (!HostReadSectorAndPosition(frameAddress, sectorBuffer, pos)) {
                                    // Read failed
                                    frameAddress = 0xFFFFFF;
                                    break;
                                }

                                // Adjust bounds
                                if (pos.track > track || (pos.track == track && pos.index >= index)) {
                                    ubFAD = frameAddress;
                                } else if (pos.track < track || (pos.track == track && pos.index < index)) {
                                    lbFAD = frameAddress + 1u;
                                }
                                frameAddress = (lbFAD + ubFAD) >> 1u;
                            }

                            SetSeekResult(cmd.data.seek.counter, frameAddress);

                            // Cache result
                            if (frameAddress != 0xFFFFFF) {
                                ts.indexFADs.insert({{track, index}, frameAddress});
                            }
                        }
                    }
                } else {
                    // Assume seek to lead-out area
                    SetSeekResult(cmd.data.seek.counter, m_toc.GetLeadOutFrameAddress());
                }
                break;
            }
            case CmdType::Quit: running = false; break;
            }
        }

        // Read and cache sectors + positions if requested
        if (shouldReadSectors) {
            uint32 numSectors;
            if (HostPrefetchSectors(ts.nextSector, sectorFetchSize, numSectors)) {
                ts.nextSector += numSectors;
            } else {
                // Could not read any sectors or reached a terminal condition; stop reading
                ts.readSectors = false;
            }
        }

        const DriveState prevDriveState = ts.driveState;
        ts.driveState = host::PollDriveState(m_devHandle);

        if (prevDriveState != ts.driveState &&
            (prevDriveState == DriveState::MediaPresent || ts.driveState == DriveState::MediaPresent)) {
            InitializeDiscInfo();
        }
    }
}

void HostCDDevice::InitializeDiscInfo() {
    auto &ts = m_threadState;

    std::unique_lock lock{ts.mtxDiscInfo};

    if (ts.driveState == DriveState::MediaPresent) {
        std::array<uint8, 2352> headerSector{};
        DiscPosition pos{};
        if (HostReadSectorAndPosition(150, headerSector, pos)) {
            ts.header.ReadFrom(std::span{headerSector}.subspan(0x10));
        } else {
            ts.header.Invalidate();
        }
        ts.toc.LoadFrom(HostReadTOC());
        if (ts.fs.Read(m_fsReader)) {
            devlog::info<grp::host>("Filesystem built successfully");
        } else {
            devlog::warn<grp::host>("Failed to build filesystem");
        }
    } else {
        ts.header.Invalidate();
        ts.toc.Clear();
        ts.fs.Clear();
        devlog::info<grp::host>("Disc absent - filesystem cleared");
    }
    ts.indexFADs.clear();
    m_sectorCache.Flush();
    ts.discInfoChanged.store(true, std::memory_order_release);
    ts.mediaStateChanged.store(true, std::memory_order_release);
    ts.targetDriveState = ts.driveState;
}

void HostCDDevice::SetSeekResult(uint32 counter, uint32 frameAddress) {
    auto &ts = m_threadState;
    ts.seekFAD.store(std::max(frameAddress, 150u), std::memory_order_release);
    ts.seekCounter.store(counter, std::memory_order_release);
    if (frameAddress != 0xFFFFFF) {
        StartReadingSectors(frameAddress);
    }
}

void HostCDDevice::StartReadingSectors(uint32 frameAddress) {
    auto &ts = m_threadState;
    ts.nextSector = frameAddress;
    ts.readSectors = true;
}

void HostCDDevice::StopReadingSectors() {
    auto &ts = m_threadState;
    ts.readSectors = false;
}

auto HostCDDevice::TryGetCachedSector(uint32 frameAddress) -> std::shared_ptr<SectorCache::Entry> {
    auto entry = m_sectorCache.Get(frameAddress - 150);
    if (entry == nullptr) {
        // No such entry, try reading directly
        uint32 numSectors;
        if (!HostPrefetchSectors(frameAddress, 1, numSectors)) {
            // Failed to read the sector; bail out
            return nullptr;
        }

        entry = m_sectorCache.Get(frameAddress - 150);
        if (entry == nullptr) {
            // Still nothing; bail out
            return nullptr;
        }
    }
    return entry;
}

std::vector<TOCEntry> HostCDDevice::HostReadTOC() const {
    std::vector<uint8> buffer{};
    buffer.resize(8);

    // Build Read TOC command
    auto cdb = scsi::op::MakeReadTOC(buffer.size());
    uint32 readSize{};

    // Execute once to get required buffer size
    if (!host::SendSCSIInCommand(m_devHandle, cdb, buffer, readSize)) {
        return {};
    }

    // Redo the request with a buffer large enough to fit the data
    const auto bufferLength = util::ReadBE<uint16>(&buffer[0]);
    buffer.resize(bufferLength + 2);
    util::WriteBE<uint16>(&cdb[7], buffer.size());
    if (!host::SendSCSIInCommand(m_devHandle, cdb, buffer, readSize)) {
        return {};
    }

    const uint8 firstTrackNum = buffer[2];
    const uint8 lastTrackNum = buffer[3];

    // Convert to TOCEntry list
    std::vector<TOCEntry> toc{};

    // Point A0 - first data track
    {
        auto &tocEntry = toc.emplace_back();
        tocEntry.controlADR = 0x41;
        tocEntry.trackNum = 0x00;
        tocEntry.pointOrIndex = 0xA0;
        tocEntry.min = 0x00;
        tocEntry.sec = 0x00;
        tocEntry.frame = 0x00;
        tocEntry.zero = 0x00;
        tocEntry.amin = util::to_bcd(firstTrackNum);
        tocEntry.asec = 0x00;
        tocEntry.aframe = 0x00;
    }

    // Point A1 - last data track
    {
        auto &tocEntry = toc.emplace_back();
        tocEntry.controlADR = 0x41;
        tocEntry.trackNum = 0x00;
        tocEntry.pointOrIndex = 0xA1;
        tocEntry.min = 0x00;
        tocEntry.sec = 0x00;
        tocEntry.frame = 0x00;
        tocEntry.zero = 0x00;
        tocEntry.amin = util::to_bcd(lastTrackNum);
        tocEntry.asec = 0x00;
        tocEntry.aframe = 0x00;
    }

    // Point A2 - start of leadout track
    // Filled in the loop below
    toc.emplace_back();

    // Tracks
    size_t pos = 4;
    const size_t totalSize = bufferLength + 2;
    while (pos + 8 <= totalSize) {
        const uint8 *trackData = &buffer[pos];

        const uint8 control = bit::extract<0, 3>(trackData[1]);
        const uint8 adr = bit::extract<4, 7>(trackData[1]);
        const uint8 trackNum = trackData[2];
        const uint32 fad = util::ReadBE<uint32>(&trackData[4]) + 150;

        if (trackNum == 0xAA) {
            auto &leadoutEntry = toc[2];
            leadoutEntry.controlADR = 0x41;
            leadoutEntry.trackNum = 0x00;
            leadoutEntry.pointOrIndex = 0xA2;
            leadoutEntry.min = 0x00;
            leadoutEntry.sec = 0x00;
            leadoutEntry.frame = 0x00;
            leadoutEntry.zero = 0x00;
            leadoutEntry.amin = util::to_bcd(fad / 75 / 60);
            leadoutEntry.asec = util::to_bcd(fad / 75 % 60);
            leadoutEntry.aframe = util::to_bcd(fad % 75);
        } else {
            const uint32 relFAD = 0; // TODO: find in image
            auto &tocEntry = toc.emplace_back();
            tocEntry.controlADR = (control << 4u) | adr;
            tocEntry.trackNum = 0x00;
            tocEntry.pointOrIndex = util::to_bcd(trackNum);
            tocEntry.min = util::to_bcd(relFAD / 75 / 60);
            tocEntry.sec = util::to_bcd(relFAD / 75 % 60);
            tocEntry.frame = util::to_bcd(relFAD % 75);
            tocEntry.zero = 0x00;
            tocEntry.amin = util::to_bcd(fad / 75 / 60);
            tocEntry.asec = util::to_bcd(fad / 75 % 60);
            tocEntry.aframe = util::to_bcd(fad % 75);
        }
        pos += 8;
    }

    return toc;
}

bool HostCDDevice::HostReadSectorAndPosition(uint32 frameAddress, std::span<uint8, 2352> outData,
                                             DiscPosition &outPos) {
    frameAddress -= 150;

    // Get cached sector + position if available
    {
        auto entry = m_sectorCache.Get(frameAddress);
        if (entry != nullptr) {
            std::copy_n(entry->sector.cbegin(), 2352, outData.begin());
            outPos = entry->pos;
            return true;
        }
    }

    std::array<uint8, 2352 + 96> data{};
    const auto cdb = scsi::op::MakeReadCD(frameAddress, 1, 0xF8, 1);
    uint32 readSize;
    if (!host::SendSCSIInCommand(m_devHandle, cdb, data, readSize)) {
        return false;
    }
    if (readSize < data.size()) {
        // We expect to be able to read the full raw sector and the P-W raw subcode data
        return false;
    }

    // Copy sector data to output
    std::copy_n(data.cbegin(), 2352, outData.begin());

    // Raw subcode data comes at the end of the buffer
    std::span<const uint8> subcode{data.begin() + 2352, data.end()};

    // Extract position data from Q subchannel
    std::array<uint8, 12> subQ{};
    for (uint32 i = 0; i < 96; ++i) {
        subQ[i >> 3u] |= bit::extract<6>(subcode[i]) << (~i & 7u);
    }

    outPos.controlADR = subQ[0];
    outPos.track = subQ[1];
    outPos.index = subQ[2];
    outPos.min = subQ[3];
    outPos.sec = subQ[4];
    outPos.frame = subQ[5];
    outPos.zero = subQ[6];
    outPos.amin = subQ[7];
    outPos.asec = subQ[8];
    outPos.aframe = subQ[9];

    // Cache sector
    {
        auto entry = m_sectorCache.Emplace(frameAddress);
        std::copy_n(data.cbegin(), 2352, entry->sector.begin());
        std::copy_n(data.cbegin() + 2352, 96, entry->subchannel.begin());
        entry->pos = outPos;
    }

    return true;
}

bool HostCDDevice::HostReadSectorUserData(uint32 frameAddress, std::span<uint8, 2048> outSector) const {
    frameAddress -= 150;
    const auto cdb = scsi::op::MakeRead10(frameAddress, 1);
    uint32 readSize;
    if (!host::SendSCSIInCommand(m_devHandle, cdb, outSector, readSize)) {
        return false;
    }
    return readSize == outSector.size();
}

bool HostCDDevice::HostPrefetchSectors(uint32 frameAddress, uint32 sectorCount, uint32 &outReadSectors) {
    if (sectorCount == 0u) {
        // Bail out immediately if no sectors were requested
        return false;
    }

    frameAddress -= 150;

    static constexpr size_t kSectorSize = 2352 + 96;

    // Don't read past the end of the current track
    const uint32 endFrameAddress = frameAddress + sectorCount - 1u;
    const TrackInfo *trackInfo = m_toc.GetTrackInfoForFAD(frameAddress);
    const uint32 trackEndFrameAddress = trackInfo != nullptr ? trackInfo->endFrameAddress : m_toc.GetEndFrameAddress();
    if (endFrameAddress > trackEndFrameAddress) {
        const uint32 extraSectors = trackEndFrameAddress - endFrameAddress;
        if (extraSectors >= sectorCount) {
            return false;
        }
        sectorCount -= extraSectors;
    }

    // Issue READ CD command
    std::vector<uint8> data{};
    data.resize(kSectorSize * sectorCount);
    const auto cdb = scsi::op::MakeReadCD(frameAddress, sectorCount, 0xF8, 1);
    uint32 readSize;
    if (!host::SendSCSIInCommand(m_devHandle, cdb, data, readSize)) {
        return false;
    }
    if (readSize % kSectorSize != 0) {
        // Bail out if we find unexpected sector sizes
        return false;
    }

    // Read sectors and parse subchannel data
    uint32 sectorIndex = 0u;
    while (sectorIndex * kSectorSize < readSize) {
        const uint32 offset = sectorIndex * kSectorSize;
        auto entry = m_sectorCache.Emplace(frameAddress + offset);

        // Copy sector data to cache
        std::copy_n(data.cbegin() + offset, 2352, entry->sector.begin());
        std::copy_n(data.cbegin() + offset + 2352, 96, entry->subchannel.begin());

        // Extract position data from Q subchannel
        std::array<uint8, 12> subQ{};
        for (uint32 i = 0; i < 96; ++i) {
            subQ[i >> 3u] |= bit::extract<6>(entry->subchannel[i]) << (~i & 7u);
        }
        DiscPosition &pos = entry->pos;
        pos.controlADR = subQ[0];
        pos.track = subQ[1];
        pos.index = subQ[2];
        pos.min = subQ[3];
        pos.sec = subQ[4];
        pos.frame = subQ[5];
        pos.zero = subQ[6];
        pos.amin = subQ[7];
        pos.asec = subQ[8];
        pos.aframe = subQ[9];

        ++sectorIndex;
    }

    outReadSectors = sectorIndex;
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------

void HostCDDevice::SectorCache::SetCapacity(size_t capacity) {
    std::unique_lock lock{m_mtx};

    m_capacity = std::max<size_t>(1, capacity);

    // Erase excess items
    while (m_entryList.size() > capacity) {
        std::shared_ptr<Entry> value = m_entryList.front();
        m_entryMap.erase(value->frameAddress);
        m_entryList.pop_front();
    }
}

size_t HostCDDevice::SectorCache::Size() const {
    std::unique_lock lock{m_mtx};
    return m_entryMap.size();
}

void HostCDDevice::SectorCache::Flush() {
    std::unique_lock lock{m_mtx};

    m_entryList.clear();
    m_entryMap.clear();
}

auto HostCDDevice::SectorCache::Get(uint32 frameAddress) -> std::shared_ptr<Entry> {
    std::unique_lock lock{m_mtx};

    // Find cached item
    auto it = m_entryMap.find(frameAddress);
    if (it == m_entryMap.end()) {
        // No such item
        return nullptr;
    }

    // Update LRU order
    m_entryList.splice(m_entryList.begin(), m_entryList, it->second);

    return *it->second;
}

auto HostCDDevice::SectorCache::Emplace(uint32 frameAddress) -> std::shared_ptr<Entry> {
    std::unique_lock lock{m_mtx};

    auto entry = std::make_shared<Entry>(frameAddress);

    // Check for existing entry
    auto it = m_entryMap.find(frameAddress);
    if (it != m_entryMap.end()) {
        // Replace it
        it->second->swap(entry);
        m_entryList.splice(m_entryList.begin(), m_entryList, it->second);
        return entry;
    }

    // Check capacity
    if (m_entryMap.size() >= m_capacity) {
        // Make room for new entry
        const uint32 oldestKey = m_entryList.back()->frameAddress;
        m_entryList.pop_back();
        m_entryMap.erase(oldestKey);
    }

    // Add entry to the top of the LRU sequence
    m_entryList.emplace_front(entry);
    m_entryMap[frameAddress] = m_entryList.begin();

    return entry;
}

bool HostCDDevice::SectorCache::Evict(uint32 frameAddress) {
    std::unique_lock lock{m_mtx};

    auto it = m_entryMap.find(frameAddress);
    if (it == m_entryMap.end()) {
        return false;
    }

    m_entryList.erase(it->second);
    m_entryMap.erase(frameAddress);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------

bool HostCDDevice::FilesystemReader::HasDisc() const {
    return m_dev.m_threadState.driveState == DriveState::MediaPresent;
}

const TOC &HostCDDevice::FilesystemReader::GetTOC() const {
    return m_dev.m_threadState.toc;
}

const SaturnHeader &HostCDDevice::FilesystemReader::GetDiscHeader() const {
    return m_dev.m_threadState.header;
}

bool HostCDDevice::FilesystemReader::ReadSectorUserData(uint32 frameAddress, std::span<uint8, 2048> outSector) {
    return m_dev.HostReadSectorUserData(frameAddress, outSector);
}

} // namespace ymir::media
