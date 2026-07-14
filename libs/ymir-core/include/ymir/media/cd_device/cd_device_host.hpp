#pragma once

/**
@file
@brief Defines `HostCDInterface`, a CD device that connects to a physical CD drive on the host.
See also @ref ymir/media/host_cd.hpp.
*/

#include "cd_device_base.hpp"

#include <ymir/media/cd_interface_callbacks.hpp>
#include <ymir/media/host_cd.hpp>

#include <ymir/util/bit_ops.hpp>

#include <blockingconcurrentqueue.h>

#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <list>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>

namespace ymir::media {

/// @brief Implements a CD device that connects to a physical CD drive on the host.
class HostCDDevice final : public ICDDevice {
public:
    /// @brief Creates a host CD device from the specified path.
    /// @param[in] path the native device path
    /// @param[in] timeout how long to wait until device is ready
    /// @param[in] cbOnMediaChanged a reference to the callback to invoke when the media presence state has changed
    HostCDDevice(std::string path, std::chrono::milliseconds timeout, const CBOnMediaChanged &cbOnMediaChanged);

    ~HostCDDevice();

    /// @brief Checks if the device was successfully connected.
    /// @return `true` if connected successfully, `false` if not
    bool IsConnected() const;

    bool ReadPosition(uint32 frameAddress, DiscPosition &outPosition) override;

    [[nodiscard]] bool IsSeekDone() const override;
    [[nodiscard]] uint32 GetSeekFrameAddress() const override;

    void HintStop() override;

protected:
    DriveState PollDriveStateImpl() override;

    uint32 ReadSectorImpl(uint32 frameAddress, std::span<uint8, 2352> out, DiscPosition *outPosition) override;
    uint32 ReadSectorUserDataImpl(uint32 frameAddress, std::span<uint8, 2048> out) override;

    void BeginSeekToFrameAddressImpl(uint32 frameAddress) override;
    void BeginSeekToTrackIndexImpl(uint8 track, uint8 index) override;

private:
    host::DeviceHandle m_devHandle = host::kInvalidDeviceHandle;
    const CBOnMediaChanged &m_cbOnMediaChanged;
    std::string m_path; // for debugging purposes only

    DriveState m_driveState = DriveState::Unknown;

    struct SeekState {
        // Seek request target:
        //   bit 31: 0=FAD, 1=track:index
        //   for FAD, bits 0-23=FAD
        //   for T:I, bits 8-15=track, bits 0-7=index
        // 0xFFFFFFFF means no requests sent
        uint32 requestedCount = 0;      // how many seek requests were enqueued
        uint32 committedCount = 0;      // how many seek requests were executed
        uint32 frameAddress = 0xFFFFFF; // seek result FAD
    } m_seekState;

    /// @brief Cacheable sector entry.
    struct SectorEntry {
        explicit SectorEntry(uint32 frameAddress)
            : frameAddress(frameAddress) {}

        uint32 frameAddress;
        std::array<uint8, 2352> sector{};
        std::array<uint8, 96> subchannel{};
        DiscPosition pos{};
    };

    /// @brief A simple LRU cache for recently used sector data.
    /// Not thread-safe. Meant to be used by emulator-facing functions only, *not* the worker thread.
    struct SectorCache {
        /// @brief Retrieves the current capacity of the cache.
        /// @return the maximum number of entries allowed in the cache
        size_t GetCapacity() const {
            return m_capacity;
        }

        /// @brief Adjusts the capacity of the cache.
        /// If smaller than the previous capacity, the oldest entries are dropped.
        /// @param[in] capacity the new cache capacity. The minimum capacity is 1.
        void SetCapacity(size_t capacity);

        /// @brief Retrieves the number of items stored in the cache.
        /// @return the current cache size
        size_t Size() const;

        /// @brief Flushes all entries in the cache.
        void Flush();

        /// @brief Retrieves an entry from the cache.
        /// @param[in] frameAddress the frame address of the entry
        /// @return a shared pointer to the entry. `nullptr` if no such entry exists
        std::shared_ptr<SectorEntry> Get(uint32 frameAddress);

        /// @brief Inserts an entry in the cache.
        /// If the cache is already at capacity, the oldest entry is removed.
        /// @param[in] frameAddress the frame address of the entry
        /// @param[in] entry the entry to insert
        void Put(uint32 frameAddress, std::shared_ptr<SectorEntry> entry);

        /// @brief Evicts (removes) an entry from the cache.
        /// @param[in] frameAddress the frame address of the entry
        /// @return `true` if the entry was removed, `false` if it did not exist
        bool Evict(uint32 frameAddress);

    private:
        /// @brief Maximum number of entries allowed in the cache.
        size_t m_capacity = 2048; // holds about 4.6 MB of sector data by default

        /// @brief Holds the entries and the LRU order (newest in front).
        std::list<std::shared_ptr<SectorEntry>> m_entryList;

        /// @brief Maps frame addresses to list entries, enabling O(1) lookups.
        std::unordered_map<uint32, typename decltype(m_entryList)::iterator> m_entryMap;
    } m_sectorCache;

    /// @brief A thread-safe cache for recently requested sectors.
    /// Thread-safe. Shared between the worker and emulator threads.
    struct SectorPrefetchCache {
        /// @brief Retrieves the number of items stored in the cache.
        /// @return the current cache size
        size_t Size() const;

        /// @brief Flushes all entries in the cache.
        void Flush();

        /// @brief Retrieves an entry from the cache.
        /// @param[in] frameAddress the frame address of the entry
        /// @return a shared pointer to the entry. `nullptr` if no such entry exists
        std::shared_ptr<SectorEntry> Get(uint32 frameAddress);

        /// @brief Creates and returns an entry in the cache.
        /// @param[in] frameAddress the frame address of the entry
        /// @return a shared pointer to the entry. Replaces existing entries with a blank value.
        std::shared_ptr<SectorEntry> Emplace(uint32 frameAddress);

        /// @brief Evicts (removes) an entry from the cache.
        /// @param[in] frameAddress the frame address of the entry
        /// @return `true` if the entry was removed, `false` if it did not exist
        bool Evict(uint32 frameAddress);

    private:
        mutable std::mutex m_mtx;

        /// @brief Main cache storage.
        std::unordered_map<uint32, std::shared_ptr<SectorEntry>> m_entries;
    } m_sectorPrefetchCache;

    /// @brief Worker thread state.
    struct ThreadState {
        mutable std::mutex mtxDiscInfo{};
        TOC toc{};
        SaturnHeader header{};
        fs::Filesystem fs{};
        std::atomic_bool discInfoChanged = false;

        // driveState contains the latest state polled from the hardware, for internal use only.
        // targetDriveState contants the state to be returned in the next PollDriveState() call.
        // The separation allows enforcing the PollDriveState() contract that requires the TOC and header to be fully
        // updated when the function returns DriveState::MediaPresent. This is done by only updating targetDriveState
        // once the operation fully completes.

        DriveState driveState = DriveState::Unknown;
        DriveState targetDriveState = DriveState::Unknown;
        std::atomic_bool mediaStateChanged = false;

        std::atomic_uint32_t seekCounter = 0; // last seek request counter executed
        std::atomic_uint32_t seekFAD = 0;     // last seek target FAD

        bool readSectors = false; // actively reading sectors?
        uint32 nextSector = 0;    // next sector to read

        struct TrackIndex {
            uint8 track;
            uint8 index;

            constexpr bool operator==(const TrackIndex &) const = default;
        };

        struct TrackIndexHash {
            std::size_t operator()(const TrackIndex &ti) const {
                return (static_cast<std::size_t>(ti.track) << 8u) | ti.index;
            }
        };

        // Caches {track, index} -> frame address
        std::unordered_map<TrackIndex, uint32, TrackIndexHash> indexFADs;
    } m_threadState;

    std::thread m_workerThread;

    struct Command {
        enum class Type {
            WakeUp,
            SeekFrameAddress,
            SeekTrackIndex,
            StopPrefetcher,
            InvokeFunction,
            InvokeFunctionWithResult,
            Quit,
        };
        Type type;

        struct SeekData {
            uint32 counter;
            union Target {
                uint32 frameAddress;
                struct {
                    uint8 track;
                    uint8 index;
                };
            } target;
        };

        struct FunctionData {
            std::function<void()> function;
        };

        std::variant<std::monostate, SeekData, FunctionData> data;

        static Command WakeUp() {
            return {.type = Type::WakeUp};
        }

        static Command SeekFrameAddress(uint32 counter, uint32 frameAddress) {
            return {.type = Type::SeekFrameAddress,
                    .data =
                        SeekData{.counter = counter, .target = {.frameAddress = bit::extract<0, 23>(frameAddress)}}};
        }

        static Command SeekTrackIndex(uint32 counter, uint8 track, uint8 index) {
            return {.type = Type::SeekTrackIndex,
                    .data = SeekData{.counter = counter, .target = {.track = track, .index = index}}};
        }

        static Command StopPrefetcher() {
            return {.type = Type::StopPrefetcher};
        }

        static Command InvokeFunction(std::function<void()> &&fn) {
            return {.type = Type::InvokeFunction, .data = FunctionData{.function = fn}};
        }

        static Command Quit() {
            return {.type = Type::Quit};
        }
    };

    moodycamel::BlockingConcurrentQueue<Command> m_workQueue;
    moodycamel::ProducerToken m_ptokWorkQueue{m_workQueue};
    moodycamel::ConsumerToken m_ctokWorkQueue{m_workQueue};

    /// @brief Disconnects the device and releases all resources.
    void Disconnect();

    /// @brief Retrieves a cached sector. If not cached, fetches the sector and caches it.
    /// @param[in] frameAddress the frame address of the sector
    /// @return a shared pointer to the cached sector data
    std::shared_ptr<SectorEntry> GetCachedSector(uint32 frameAddress);

    /// @brief Enqueues a command for execution in the worker thread.
    /// @param[in] cmd the command to enqueue
    void EnqueueCommand(Command &&cmd);

    /// @brief Enqueues a command to execute the given function in the worker thread.
    /// @tparam TFn the function type
    /// @param[in] fn the function to execute
    template <typename TFn>
        requires std::is_void_v<std::invoke_result_t<TFn>>
    void ExecuteInWorker(TFn &&fn);

    /// @brief Executes the given function in the worker thread and waits for the result.
    /// @tparam TFn the function type
    /// @param[in] fn the function to execute
    /// @return the return value of the function
    template <typename TFn>
        requires(!std::is_void_v<std::invoke_result_t<TFn>>)
    typename std::invoke_result_t<TFn> ExecuteInWorkerSync(TFn &&fn);

    /// @brief Enqueues the function to be executed in the worker thread asynchronously.
    /// @tparam TFn the function type
    /// @param[in] fn the function to execute
    /// @return the a future with the return value of the function
    template <typename TFn>
        requires(!std::is_void_v<std::invoke_result_t<TFn>>)
    std::future<typename std::invoke_result_t<TFn>> ExecuteInWorkerAsync(TFn &&fn);

    /// @brief Worker thread entrypoint.
    void WorkerThread();

    /// @brief Initializes worker-side disc data structures.
    void InitializeWorkerDiscInfo();

    /// @brief Clears emulator-side disc data structures.
    void ClearMainDiscInfo();

    /// @brief Clears worker-side disc data structures.
    void ClearWorkerDiscInfo();

    /// @brief Sets the result of the seek operation.
    /// Invoked by the worker thread.
    /// @param[in] counter the seek command counter
    /// @param[in] frameAddress the resulting frame address
    void SetSeekResult(uint32 counter, uint32 frameAddress);

    /// @brief Starts prefetching sectors sequentially from the given frame address
    /// @param[in] frameAddress the frame address to start prefetching sectors from
    void StartPrefetcher(uint32 frameAddress);

    /// @brief Stops prefetching sectors.
    void StopPrefetcher();

    /// @brief Sends a command to the host device to read the disc's table of contents.
    /// @return the table of contents. Empty if no disc or if the command failed
    std::vector<TOCEntry> HostReadTOC() const;

    /// @brief Sends a command to the host device to read the raw sector data and position information from a frame
    /// address.
    /// @param[in] frameAddress the frame address
    /// @param[out] outSector receives store sector data
    /// @param[out] outPos receives position data
    /// @return `true` if the sector was successfully read, `false` if not
    bool HostReadSectorAndPosition(uint32 frameAddress, std::span<uint8, 2352> outSector, DiscPosition &outPos);

    /// @brief Sends a command to the host device to read the sector's user data area.
    /// @param[in] frameAddress the frame address
    /// @param[out] outSector receives store sector data
    /// @return `true` if the sector was successfully read, `false` if not
    bool HostReadSectorUserData(uint32 frameAddress, std::span<uint8, 2048> outSector) const;

    /// @brief Sends a command to the host device to read the raw sector and subchannel for cache storage.
    /// @param[in] frameAddress the frame address
    /// @param[out] outEntry the cache entry to store all data into
    /// @return `true` if the sector was successfully read, `false` if not
    bool HostReadSectorForCache(uint32 frameAddress, SectorEntry &outEntry) const;

    /// @brief Prefetches and caches a number of sectors starting at the specified frame address.
    /// @param[in] frameAddress the starting frame address
    /// @param[in] sectorCount the number of sectors to read
    /// @param[out] outReadSectors receives the number of sectors read
    /// @return `true` if more sectors can be read, `false` if a terminal condition was reached
    bool HostPrefetchSectors(uint32 frameAddress, uint32 sectorCount, uint32 &outReadSectors);

    // -----------------------------------------------------------------------------------------------------------------

    struct FilesystemReader : fs::IFilesystemCDReader {
        FilesystemReader(HostCDDevice &dev)
            : m_dev(dev) {}

        [[nodiscard]] bool HasDisc() const override;
        [[nodiscard]] const TOC &GetTOC() const override;
        bool ReadSectorUserData(uint32 frameAddress, std::span<uint8, 2048> outSector) override;

    private:
        HostCDDevice &m_dev;
    } m_fsReader{*this};
};

} // namespace ymir::media
