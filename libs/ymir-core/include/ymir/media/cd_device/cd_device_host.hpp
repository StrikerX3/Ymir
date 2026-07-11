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

#include <atomic>
#include <mutex>
#include <thread>

namespace ymir::media {

/// @brief Implements a CD device that connects to a physical CD drive on the host.
class HostCDDevice final : public ICDDevice {
public:
    /// @brief Creates a host CD device from the specified path.
    /// @param[in] path the native device path. Accepted path formats vary per operating system:
    /// - Windows: drive letters ("D:"), NT device paths ("\Device\CdRom0") or DOS paths ("\\.\D:", "\\.\CdRom0")
    /// - Linux: SCSI generic device paths ("/dev/sg0")
    /// - Other systems: TBD
    /// @param[in] cbOnMediaChanged a reference to the callback to invoke when the media presence state has changed
    HostCDDevice(std::string path, const CBOnMediaChanged &cbOnMediaChanged);

    ~HostCDDevice();

    /// @brief Checks if the device was successfully connected.
    /// @return `true` if connected successfully, `false` if not
    bool IsConnected() const;

    bool ReadPosition(uint32 frameAddress, DiscPosition &outPosition) override;

    [[nodiscard]] bool IsSeekDone() const override;
    [[nodiscard]] uint32 GetSeekFrameAddress() const override;

protected:
    DriveState PollDriveStateImpl() override;

    uint32 ReadSectorImpl(uint32 frameAddress, std::span<uint8, 2352> out) override;
    uint32 ReadSectorUserDataImpl(uint32 frameAddress, std::span<uint8, 2048> out) override;

    void BeginSeekToFrameAddressImpl(uint32 frameAddress) override;
    void BeginSeekToTrackIndexImpl(uint8 track, uint8 index) override;

private:
    host::DeviceHandle m_devHandle = host::kInvalidDeviceHandle;
    const CBOnMediaChanged &m_cbOnMediaChanged;

    DriveState m_driveState = DriveState::Unknown;

    struct SeekState {
        // Seek request target:
        //   bit 31: 0=FAD, 1=track:index
        //   for FAD, bits 0-23=FAD
        //   for T:I, bits 8-15=track, bits 0-7=index
        // 0xFFFFFFFF means no requests sent
        uint32 requestedTarget = 0xFFFFFFFF;
        uint32 requestedCount = 0;      // how many seek requests were enqueued
        uint32 committedCount = 0;      // how many seek requests were executed
        uint32 frameAddress = 0xFFFFFF; // seek result FAD
    } m_seekState;

    struct ThreadState {
        mutable std::mutex mtxDiscInfo{};
        TOC toc{};
        SaturnHeader header{};
        fs::Filesystem fs{};
        std::atomic_bool discInfoChanged = false;

        // driveState contains the latest state polled from the hardware, for internal use only.
        // targetDriveState contants the state to be returned in the next PollDriveState() call.
        // The separation allows enforcing the PollDriveState() contract that requires the TOC and header to be fully
        // updated when the function returns DriveState::MediaPresent.

        DriveState driveState = DriveState::Unknown;
        DriveState targetDriveState = DriveState::Unknown;
        std::atomic_bool mediaStateChanged = false;

        std::atomic_uint32_t seekCounter = 0; // last seek request counter executed
        std::atomic_uint32_t seekFAD = 0;     // last seek target FAD
    } m_threadState;

    std::thread m_workerThread;

    struct Command {
        enum class Type { SeekFrameAddress, SeekTrackIndex, Quit };
        Type type;
        union Data {
            struct Seek {
                uint32 counter;
                union Target {
                    uint32 frameAddress;
                    struct {
                        uint8 track;
                        uint8 index;
                    };
                } target;
            } seek;
        } data;

        static Command SeekFrameAddress(uint32 counter, uint32 frameAddress) {
            return {
                .type = Type::SeekFrameAddress,
                .data = {.seek = {.counter = counter, .target = {.frameAddress = bit::extract<0, 23>(frameAddress)}}}};
        }

        static Command SeekTrackIndex(uint32 counter, uint8 track, uint8 index) {
            return {.type = Type::SeekTrackIndex,
                    .data = {.seek = {.counter = counter, .target = {.track = track, .index = index}}}};
        }

        static Command Quit() {
            return {.type = Type::Quit};
        }
    };

    moodycamel::BlockingConcurrentQueue<Command> m_workQueue;
    moodycamel::ProducerToken m_ptokWorkQueue{m_workQueue};
    moodycamel::ConsumerToken m_ctokWorkQueue{m_workQueue};

    void EnqueueCommand(Command &&cmd);

    void WorkerThread();

    void ReadHeaderAndTOC();
    void SetSeekResult(uint32 counter, uint32 frameAddress);

    std::vector<TOCEntry> HostReadTOC() const;
    bool HostReadSectorAndPosition(uint32 frameAddress, std::span<uint8, 2352> outData, DiscPosition &outPos);
};

} // namespace ymir::media
