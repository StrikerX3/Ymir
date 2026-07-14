#include <ymir/media/cd_device/cd_device_host.hpp>

#include <ymir/media/scsi.hpp>

#include <ymir/util/arith_ops.hpp>
#include <ymir/util/dev_log.hpp>
#include <ymir/util/thread_name.hpp>

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
    //   cache

    struct host {
        static constexpr bool enabled = true;
        static constexpr devlog::Level level = devlog::level::debug;
        static constexpr std::string_view name = "CDDev-Host";
    };

    struct cache : public host {
        static constexpr devlog::Level level = devlog::level::debug;
        static constexpr std::string_view name = "CDDev-Host-Cache";
    };

} // namespace grp

HostCDDevice::HostCDDevice(std::string path, std::chrono::milliseconds timeout,
                           const CBOnMediaChanged &cbOnMediaChanged)
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

    // Wait for the specified amount of time for device to be ready
    using namespace std::chrono_literals;
    using clk = std::chrono::steady_clock;
    const auto t0 = clk::now();
    const auto deadline = t0 + timeout;
    while (t0 < deadline) {
        if (PollDriveState() != DriveState::Unknown) {
            break;
        }
        // Wait between attempts to avoid hammering the CPU
        std::this_thread::sleep_for(10ms);
    }

    if (m_driveState == DriveState::Unknown) {
        // Timed out
        Disconnect();
    }
}

HostCDDevice::~HostCDDevice() {
    Disconnect();
}

bool HostCDDevice::IsConnected() const {
    return m_devHandle != host::kInvalidDeviceHandle;
}

bool HostCDDevice::ReadPosition(uint32 frameAddress, DiscPosition &outPosition) {
    auto entry = GetCachedSector(frameAddress);
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

void HostCDDevice::HintStop() {
    EnqueueCommand(Command::StopPrefetcher());
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
        m_sectorCache.Flush();
        m_cbOnMediaChanged();
    }

    m_seekState.frameAddress = ts.seekFAD.load(std::memory_order_acquire);
    m_seekState.committedCount = ts.seekCounter.load(std::memory_order_acquire);

    return m_driveState;
}

uint32 HostCDDevice::ReadSectorImpl(uint32 frameAddress, std::span<uint8, 2352> outSector, DiscPosition *outPosition) {
    auto entry = GetCachedSector(frameAddress);
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
    auto entry = GetCachedSector(frameAddress);
    if (entry == nullptr) {
        return 0u;
    }

    std::copy_n(entry->sector.begin() + 0x10, outSector.size(), outSector.begin());
    return outSector.size();
}

void HostCDDevice::BeginSeekToFrameAddressImpl(uint32 frameAddress) {
    const uint32 target = bit::extract<0, 23>(frameAddress);
    ++m_seekState.requestedCount;
    EnqueueCommand(Command::SeekFrameAddress(m_seekState.requestedCount, frameAddress));
}

void HostCDDevice::BeginSeekToTrackIndexImpl(uint8 track, uint8 index) {
    const uint32 target = (1u << 31u) | (track << 8u) | index;
    ++m_seekState.requestedCount;
    EnqueueCommand(Command::SeekTrackIndex(m_seekState.requestedCount, track, index));
}

void HostCDDevice::Disconnect() {
    if (m_workerThread.joinable()) {
        EnqueueCommand(Command::Quit());
        m_workerThread.join();
    }
    // TODO: cleanup other resources (notification handles, etc.)
    if (m_devHandle != host::kInvalidDeviceHandle) {
        host::CloseDeviceHandle(m_devHandle);
        m_devHandle = host::kInvalidDeviceHandle;
    }
    ClearMainDiscInfo();
}

auto HostCDDevice::GetCachedSector(uint32 frameAddress) -> std::shared_ptr<SectorEntry> {
    // Look in LRU cache
    std::shared_ptr<SectorEntry> entry = m_sectorCache.Get(frameAddress);
    if (entry != nullptr) {
        devlog::trace<grp::cache>("{:06X} found in LRU cache", frameAddress);
        return entry;
    }

    // Look in prefetch cache
    entry = m_sectorPrefetchCache.Get(frameAddress);
    if (entry != nullptr) {
        // Found it; move to main LRU cache
        m_sectorCache.Put(frameAddress, entry);
        m_sectorPrefetchCache.Evict(frameAddress);
        EnqueueCommand(Command::WakeUp()); // unblocks the worker thread immediately
        devlog::trace<grp::cache>("{:06X} found in prefetch cache; LRU cache usage: {} of {}", frameAddress,
                                  m_sectorCache.Size(), m_sectorCache.GetCapacity());
        return entry;
    }

    // Not found in either cache
    return ExecuteInWorkerSync([&]() -> std::shared_ptr<SectorEntry> {
        // Reconfigure prefetcher
        StartPrefetcher(frameAddress + 1);
        devlog::trace<grp::cache>("{:06X} cache miss", frameAddress);

        // Read sector
        entry = std::make_shared<SectorEntry>(frameAddress);
        if (!HostReadSectorForCache(frameAddress, *entry)) {
            devlog::trace<grp::cache>("{:06X} could not be read", frameAddress);
            return nullptr;
        }
        m_sectorCache.Put(frameAddress, entry);
        devlog::trace<grp::cache>("{:06X} cached in LRU cache; usage: {} of {}", frameAddress, m_sectorCache.Size(),
                                  m_sectorCache.GetCapacity());
        return entry;
    });
}

void HostCDDevice::EnqueueCommand(Command &&cmd) {
    m_workQueue.enqueue(m_ptokWorkQueue, cmd);
}

template <typename TFn>
    requires std::is_void_v<std::invoke_result_t<TFn>>
void HostCDDevice::ExecuteInWorker(TFn &&fn) {
    EnqueueCommand(Command::InvokeFunction([&] { fn(); }));
}

template <typename TFn>
    requires(!std::is_void_v<std::invoke_result_t<TFn>>)
typename std::invoke_result_t<TFn> HostCDDevice::ExecuteInWorkerSync(TFn &&fn) {
    using TReturn = std::invoke_result_t<TFn>;
    std::promise<TReturn> promise{};
    std::future<TReturn> result = promise.get_future();
    EnqueueCommand(Command::InvokeFunction([&] { promise.set_value(fn()); }));
    return result.get();
}

template <typename TFn>
    requires(!std::is_void_v<std::invoke_result_t<TFn>>)
std::future<typename std::invoke_result_t<TFn>> HostCDDevice::ExecuteInWorkerAsync(TFn &&fn) {
    using TReturn = std::invoke_result_t<TFn>;
    auto promise = std::make_shared<std::promise<TReturn>>();
    std::future<TReturn> result = promise->get_future();
    EnqueueCommand(Command::InvokeFunction([=] { promise->set_value(fn()); }));
    return result;
}

void HostCDDevice::WorkerThread() {
    util::SetCurrentThreadName(fmt::format("{} worker", m_path).c_str());

    using namespace std::chrono_literals;

    auto &ts = m_threadState;

    ts.driveState = host::PollDriveState(m_devHandle);
    InitializeWorkerDiscInfo();

    Command cmd{};
    bool running = true;
    while (running) {
        // TODO: make this configurable
        static constexpr uint32 kMaxSectorFetchSize = 16;
        static constexpr uint32 kSectorPrefetchCapacity = 32;
        const uint32 sectorFetchSize = kSectorPrefetchCapacity - m_sectorPrefetchCache.Size();

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
            case CmdType::WakeUp: /* no-op on purpose */ break;
            case CmdType::SeekFrameAddress: //
            {
                const auto seekData = std::get<Command::SeekData>(cmd.data);
                // No need to issue SEEK command to host device for this; the frame address is (obviously) known

                const uint32 frameAddress = std::max(seekData.target.frameAddress, 150u);
                SetSeekResult(seekData.counter, frameAddress);
                break;
            }
            case CmdType::SeekTrackIndex: //
            {
                const auto seekData = std::get<Command::SeekData>(cmd.data);
                const TrackInfo *trackInfo = ts.toc.GetTrackInfoForNumber(seekData.target.track);
                if (trackInfo != nullptr) {
                    if (seekData.target.index == 1) {
                        // Trivial case: seek to start of track; target frame address is known from TOC
                        SetSeekResult(seekData.counter, trackInfo->startFrameAddress);
                    } else {
                        const uint8 track = seekData.target.track;
                        const uint8 index = seekData.target.index;

                        if (auto it = ts.indexFADs.find({track, index}); it != ts.indexFADs.end()) {
                            // Retrieve cached result
                            SetSeekResult(seekData.counter, it->second);
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
                                const uint8 posTrack = util::from_bcd(pos.track);
                                const uint8 posIndex = util::from_bcd(pos.index);
                                if (posTrack > track || (posTrack == track && posIndex >= index)) {
                                    ubFAD = frameAddress;
                                } else if (posTrack < track || (posTrack == track && posIndex < index)) {
                                    lbFAD = frameAddress + 1u;
                                }
                                frameAddress = (lbFAD + ubFAD) >> 1u;
                            }

                            SetSeekResult(seekData.counter, frameAddress);

                            // Cache result
                            if (frameAddress != 0xFFFFFF) {
                                ts.indexFADs.insert({{track, index}, frameAddress});
                            }
                        }
                    }
                } else {
                    // Assume seek to lead-out area
                    SetSeekResult(seekData.counter, ts.toc.GetLeadOutFrameAddress());
                }
                break;
            }
            case CmdType::StopPrefetcher: StopPrefetcher(); break;
            case CmdType::InvokeFunction: std::get<Command::FunctionData>(cmd.data).function(); break;
            case CmdType::InvokeFunctionWithResult: std::get<Command::FunctionData>(cmd.data).function(); break;
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
            InitializeWorkerDiscInfo();
        }
    }

    ClearWorkerDiscInfo();
}

void HostCDDevice::InitializeWorkerDiscInfo() {
    auto &ts = m_threadState;

    std::unique_lock lock{ts.mtxDiscInfo};

    m_sectorPrefetchCache.Flush();
    if (ts.driveState == DriveState::MediaPresent) {
        std::array<uint8, 2352> headerSector{};
        DiscPosition pos{};
        if (HostReadSectorAndPosition(150u, headerSector, pos)) {
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
    ts.discInfoChanged.store(true, std::memory_order_release);
    ts.mediaStateChanged.store(true, std::memory_order_release);
    ts.targetDriveState = ts.driveState;
}

void HostCDDevice::ClearMainDiscInfo() {
    m_driveState = DriveState::Unknown;
    m_header.Invalidate();
    m_toc.Clear();
    m_fs.Clear();
    m_sectorCache.Flush();
}

void HostCDDevice::ClearWorkerDiscInfo() {
    auto &ts = m_threadState;

    std::unique_lock lock{ts.mtxDiscInfo};
    m_sectorPrefetchCache.Flush();
    ts.header.Invalidate();
    ts.toc.Clear();
    ts.fs.Clear();
    ts.indexFADs.clear();
    ts.discInfoChanged.store(true, std::memory_order_release);
    ts.mediaStateChanged.store(true, std::memory_order_release);
    ts.targetDriveState = ts.driveState;
}

void HostCDDevice::SetSeekResult(uint32 counter, uint32 frameAddress) {
    auto &ts = m_threadState;
    ts.seekFAD.store(std::max(frameAddress, 150u), std::memory_order_release);
    ts.seekCounter.store(counter, std::memory_order_release);
    if (frameAddress != 0xFFFFFF) {
        StartPrefetcher(frameAddress);
    }
}

void HostCDDevice::StartPrefetcher(uint32 frameAddress) {
    auto &ts = m_threadState;
    ts.nextSector = frameAddress;
    ts.readSectors = true;
    m_sectorPrefetchCache.Flush();
    devlog::trace<grp::cache>("Prefetcher started at {:06X}", frameAddress);
}

void HostCDDevice::StopPrefetcher() {
    auto &ts = m_threadState;
    ts.readSectors = false;
    m_sectorPrefetchCache.Flush();
    devlog::trace<grp::cache>("Prefetcher stopped");
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
        const uint32 fad = util::ReadBE<uint32>(&trackData[4]) + 150u;

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

bool HostCDDevice::HostReadSectorAndPosition(uint32 frameAddress, std::span<uint8, 2352> outSector,
                                             DiscPosition &outPos) {
    std::array<uint8, 2352 + 96> data{};
    const auto cdb = scsi::op::MakeReadCD(frameAddress - 150u, 1, 0xF8, 1);
    uint32 readSize;
    if (!host::SendSCSIInCommand(m_devHandle, cdb, data, readSize)) {
        return false;
    }
    if (readSize < data.size()) {
        // We expect to be able to read the full raw sector and the P-W raw subcode data
        return false;
    }

    // Copy sector data to output
    std::copy_n(data.cbegin(), 2352, outSector.begin());

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

    return true;
}

bool HostCDDevice::HostReadSectorUserData(uint32 frameAddress, std::span<uint8, 2048> outSector) const {
    const auto cdb = scsi::op::MakeRead10(frameAddress - 150u, 1);
    uint32 readSize;
    if (!host::SendSCSIInCommand(m_devHandle, cdb, outSector, readSize)) {
        return false;
    }
    return readSize == outSector.size();
}

bool HostCDDevice::HostReadSectorForCache(uint32 frameAddress, SectorEntry &outEntry) const {
    std::array<uint8, 2352 + 96> data{};
    const auto cdb = scsi::op::MakeReadCD(frameAddress - 150u, 1, 0xF8, 1);
    uint32 readSize;
    if (!host::SendSCSIInCommand(m_devHandle, cdb, data, readSize)) {
        return false;
    }
    if (readSize < data.size()) {
        // We expect to be able to read the full raw sector and the P-W raw subcode data
        return false;
    }

    // Copy sector data to output
    std::copy_n(data.cbegin(), 2352, outEntry.sector.begin());
    std::copy_n(data.cbegin() + 2352, 96, outEntry.subchannel.begin());

    // Extract position data from Q subchannel
    std::array<uint8, 12> subQ{};
    for (uint32 i = 0; i < 96; ++i) {
        subQ[i >> 3u] |= bit::extract<6>(outEntry.subchannel[i]) << (~i & 7u);
    }
    outEntry.pos.controlADR = subQ[0];
    outEntry.pos.track = subQ[1];
    outEntry.pos.index = subQ[2];
    outEntry.pos.min = subQ[3];
    outEntry.pos.sec = subQ[4];
    outEntry.pos.frame = subQ[5];
    outEntry.pos.zero = subQ[6];
    outEntry.pos.amin = subQ[7];
    outEntry.pos.asec = subQ[8];
    outEntry.pos.aframe = subQ[9];

    return true;
}

bool HostCDDevice::HostPrefetchSectors(uint32 frameAddress, uint32 sectorCount, uint32 &outReadSectors) {
    if (sectorCount == 0u) {
        // Bail out immediately if no sectors were requested
        return false;
    }

    static constexpr size_t kSectorSize = 2352 + 96;

    auto &ts = m_threadState;

    // Don't read past the end of the current track
    const uint32 endFrameAddress = frameAddress + sectorCount - 1u;
    const TrackInfo *trackInfo = ts.toc.GetTrackInfoForFAD(frameAddress);
    const uint32 trackEndFrameAddress = trackInfo != nullptr ? trackInfo->endFrameAddress : ts.toc.GetEndFrameAddress();
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
    const auto cdb = scsi::op::MakeReadCD(frameAddress - 150u, sectorCount, 0xF8, 1);
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
        devlog::trace<grp::cache>("{:06X} prefetched, usage: {}", frameAddress + sectorIndex,
                                  m_sectorPrefetchCache.Size());
        auto entry = m_sectorPrefetchCache.Emplace(frameAddress + sectorIndex);

        // Copy sector data to cache
        const uint32 offset = sectorIndex * kSectorSize;
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
    m_capacity = std::max<size_t>(1, capacity);

    // Erase excess items
    while (m_entryList.size() > capacity) {
        std::shared_ptr<SectorEntry> value = m_entryList.front();
        m_entryMap.erase(value->frameAddress);
        m_entryList.pop_front();
    }
}

size_t HostCDDevice::SectorCache::Size() const {
    return m_entryMap.size();
}

void HostCDDevice::SectorCache::Flush() {
    m_entryList.clear();
    m_entryMap.clear();
}

auto HostCDDevice::SectorCache::Get(uint32 frameAddress) -> std::shared_ptr<SectorEntry> {
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

void HostCDDevice::SectorCache::Put(uint32 frameAddress, std::shared_ptr<SectorEntry> entry) {
    // Check for existing entry
    auto it = m_entryMap.find(frameAddress);
    if (it != m_entryMap.end()) {
        // Replace it
        *it->second = entry;
        m_entryList.splice(m_entryList.begin(), m_entryList, it->second);
        return;
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
}

bool HostCDDevice::SectorCache::Evict(uint32 frameAddress) {
    auto it = m_entryMap.find(frameAddress);
    if (it == m_entryMap.end()) {
        return false;
    }

    m_entryList.erase(it->second);
    m_entryMap.erase(frameAddress);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------

size_t HostCDDevice::SectorPrefetchCache::Size() const {
    std::unique_lock lock{m_mtx};
    return m_entries.size();
}

void HostCDDevice::SectorPrefetchCache::Flush() {
    std::unique_lock lock{m_mtx};
    m_entries.clear();
}

auto HostCDDevice::SectorPrefetchCache::Get(uint32 frameAddress) -> std::shared_ptr<SectorEntry> {
    std::unique_lock lock{m_mtx};

    // Find cached item
    auto it = m_entries.find(frameAddress);
    if (it == m_entries.end()) {
        // No such item
        return nullptr;
    }

    return it->second;
}

auto HostCDDevice::SectorPrefetchCache::Emplace(uint32 frameAddress) -> std::shared_ptr<SectorEntry> {
    std::unique_lock lock{m_mtx};

    auto entry = std::make_shared<SectorEntry>(frameAddress);

    // Check for existing entry
    auto it = m_entries.find(frameAddress);
    if (it != m_entries.end()) {
        // Replace it
        it->second.swap(entry);
        return entry;
    }

    // Add to cache
    m_entries[frameAddress] = entry;
    return entry;
}

bool HostCDDevice::SectorPrefetchCache::Evict(uint32 frameAddress) {
    std::unique_lock lock{m_mtx};
    return m_entries.erase(frameAddress) > 0;
}

// ---------------------------------------------------------------------------------------------------------------------

bool HostCDDevice::FilesystemReader::HasDisc() const {
    return m_dev.m_threadState.driveState == DriveState::MediaPresent;
}

const TOC &HostCDDevice::FilesystemReader::GetTOC() const {
    return m_dev.m_threadState.toc;
}

bool HostCDDevice::FilesystemReader::ReadSectorUserData(uint32 frameAddress, std::span<uint8, 2048> outSector) {
    return m_dev.HostReadSectorUserData(frameAddress, outSector);
}

} // namespace ymir::media
