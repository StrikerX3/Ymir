#pragma once

/**
@file
@brief Defines `HostCDInterface`, a CD device that connects to a physical CD drive on the host.
See also @ref ymir/media/host_cd.hpp.
*/

#include "cd_device_base.hpp"

#include <ymir/media/cd_interface_callbacks.hpp>
#include <ymir/media/host_cd.hpp>

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

    DriveState PollDriveState() override;

    bool ReadPosition(uint32 frameAddress, DiscPosition &outPosition) override;

    [[nodiscard]] bool IsSeekDone() const override;
    [[nodiscard]] uint32 GetSeekFrameAddress() const override;

protected:
    std::vector<TOCEntry> ReadTOC() override;

    uint32 ReadSectorImpl(uint32 frameAddress, std::span<uint8, 2352> out) override;
    uint32 ReadSectorUserDataImpl(uint32 frameAddress, std::span<uint8, 2048> out) override;

    void BeginSeekToFrameAddressImpl(uint32 frameAddress) override;
    void BeginSeekToTrackIndexImpl(uint8 trackNumber, uint8 indexNumber) override;

private:
    host::DeviceHandle m_devHandle = host::kInvalidDeviceHandle;
    const CBOnMediaChanged &m_cbOnMediaChanged;

    DriveState m_driveState = DriveState::Unknown;

    struct ThreadState {
        mutable std::mutex mtxDiscInfo{};
        TOC toc{};
        SaturnHeader header{};
        std::atomic_bool discInfoChanged = false;

        DriveState driveState = DriveState::Unknown;
        std::atomic_bool mediaStateChanged = false;
    } m_threadState;

    std::thread m_workerThread;

    struct Command {
        enum class Type { Quit };
        Type type;

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
};

} // namespace ymir::media
