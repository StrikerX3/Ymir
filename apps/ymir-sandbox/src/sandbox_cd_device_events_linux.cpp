#include <ymir/media/scsi.hpp>

#include <ymir/util/bit_ops.hpp>
#include <ymir/util/data_ops.hpp>
#include <ymir/util/dev_assert.hpp>
#include <ymir/util/scope_guard.hpp>
#include <ymir/util/thread_name.hpp>

#include <fmt/format.h>

#include <blockingconcurrentqueue.h>

#include <scsi/sg.h>
#include <sys/ioctl.h>

#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

static void PrintDrives();

namespace ymir::scsi {

FORCE_INLINE static int TranslateDirection(Direction direction) {
    switch (direction) {
    case Direction::In: return SG_DXFER_FROM_DEV;
    case Direction::Out: return SG_DXFER_TO_DEV;
    case Direction::InOut: return SG_DXFER_TO_FROM_DEV;
    case Direction::None: return SG_DXFER_FROM_DEV;
    default: return SG_DXFER_FROM_DEV;
    }
}

FORCE_INLINE static bool SendCommand(int fd, Direction direction, std::span<uint8> cdb, std::span<uint8> outData,
                                     uint32 &outSize) {
    std::array<uint8, 96> senseBuffer{};
    sg_io_hdr_t ioHdr{};
    ioHdr.interface_id = 'S';
    ioHdr.dxfer_direction = TranslateDirection(direction);
    ioHdr.cmd_len = sizeof(cdb);
    ioHdr.mx_sb_len = sizeof(senseBuffer);
    ioHdr.dxfer_len = outData.size();
    ioHdr.dxferp = outData.data();
    ioHdr.cmdp = cdb.data();
    ioHdr.sbp = senseBuffer.data();
    ioHdr.timeout = 3000;
    if (ioctl(fd, SG_IO, &ioHdr) < 0) {
        return false;
    }
    if ((ioHdr.info & SG_INFO_OK_MASK) != SG_INFO_OK) {
        return false;
    }
    outSize = ioHdr.dxfer_len - ioHdr.resid;
    return true;
}

FORCE_INLINE static bool SendInCommand(int fd, std::span<uint8> cdb, std::span<uint8> outData, uint32 &outSize) {
    return SendCommand(fd, Direction::In, cdb, outData, outSize);
}

FORCE_INLINE static bool SendOutCommand(int fd, std::span<uint8> cdb, std::span<uint8> outData, uint32 &outSize) {
    return SendCommand(fd, Direction::Out, cdb, outData, outSize);
}

FORCE_INLINE static bool SendInOutCommand(int fd, std::span<uint8> cdb, std::span<uint8> outData, uint32 &outSize) {
    return SendCommand(fd, Direction::InOut, cdb, outData, outSize);
}

FORCE_INLINE static bool SendNoDirCommand(int fd, std::span<uint8> cdb, std::span<uint8> outData, uint32 &outSize) {
    return SendCommand(fd, Direction::None, cdb, outData, outSize);
}

} // namespace ymir::scsi

struct Command {
    enum class Type { MediaStateChanged, Quit };

    Type type;
    union {
        struct {
            bool inserted;
        } mediaState;
    } data;

    static Command MediaStateChanged(bool inserted) {
        return {.type = Type::MediaStateChanged, .data = {.mediaState = {.inserted = inserted}}};
    }

    static Command Quit() {
        return {.type = Type::Quit};
    }
};

struct LinuxCDDevice {
    explicit LinuxCDDevice(const std::string &path)
        : path(path) {
        fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
        thread = std::thread([this] { ThreadProc(); });
    }

    ~LinuxCDDevice() {
        if (thread.joinable()) {
            EnqueueCommand(Command::Quit());
            thread.join();
        }
    }

    std::string path;
    int fd = -1;

    ymir::scsi::MediaPresenceState mediaPresenceState = ymir::scsi::MediaPresenceState::Unknown;
    ymir::scsi::TrayState trayState = ymir::scsi::TrayState::Unknown;

    std::thread thread;

    moodycamel::BlockingConcurrentQueue<Command> cmdQueue;
    moodycamel::ProducerToken ptokCmdQueue{cmdQueue};
    moodycamel::ConsumerToken ctokCmdQueue{cmdQueue};

    void EnqueueCommand(Command &&command) {
        cmdQueue.enqueue(ptokCmdQueue, std::move(command));
    }

    void ThreadProc() {
        util::SetCurrentThreadName(fmt::format("CDDev: {}", path).c_str());

        using namespace std::chrono_literals;
        static constexpr auto kPresentMediaStateQueryInterval = 3s;
        static constexpr auto kAbsentMediaStateQueryInterval = 1s;

        Command command;
        bool running = true;
        while (running) {
            const auto queryInterval = mediaPresenceState == ymir::scsi::MediaPresenceState::Present
                                           ? kPresentMediaStateQueryInterval
                                           : kAbsentMediaStateQueryInterval;
            if (cmdQueue.wait_dequeue_timed(ctokCmdQueue, command, queryInterval)) {
                switch (command.type) {
                case Command::Type::MediaStateChanged:
                    fmt::println("Media {} drive {}",
                                 (command.data.mediaState.inserted ? "inserted in" : "removed from"), path);
                    UpdateDriveState();
                    break;
                case Command::Type::Quit:
                    fmt::println("{} thread quitting", path);
                    running = false;
                    break;
                }
            } else {
                // Periodically check media state just in case we don't get automatic notifications
                UpdateDriveState();
            }
        }
    }

    void UpdateDriveState() {
        const ymir::scsi::MediaPresenceState prevMediaPresenceState = mediaPresenceState;
        const ymir::scsi::TrayState prevTrayState = trayState;

        util::ScopeGuard sgPrintState{[&] {
            // Check for state changes
            if (mediaPresenceState != prevMediaPresenceState) {
                std::string_view mediaPresenceStateStr = [&] {
                    switch (mediaPresenceState) {
                    case ymir::scsi::MediaPresenceState::Absent: return "no media";
                    case ymir::scsi::MediaPresenceState::Present: return "media present";
                    case ymir::scsi::MediaPresenceState::Unknown: return "unknown media state";
                    default: return "invalid media state";
                    }
                }();
                fmt::println("Device {} media state changed: {}", path, mediaPresenceStateStr);
            }
            if (trayState != prevTrayState) {
                std::string_view trayStateStr = [&] {
                    switch (trayState) {
                    case ymir::scsi::TrayState::Open: return "tray open";
                    case ymir::scsi::TrayState::Closed: return "tray closed";
                    case ymir::scsi::TrayState::Unknown: return "unknown tray state";
                    default: return "invalid tray state";
                    }
                }();
                fmt::println("Device {} tray state changed: {}", path, trayStateStr);
            }
        }};

        mediaPresenceState = ymir::scsi::MediaPresenceState::Unknown;
        trayState = ymir::scsi::TrayState::Unknown;

        std::array<uint8, 128> buffer{};
        auto cdb =
            ymir::scsi::op::MakeGetEventStatusNotification(true, ymir::scsi::notif_class::kMediaStatus, buffer.size());
        uint32 outSize = 0;
        if (!ymir::scsi::SendInCommand(fd, cdb, buffer, outSize)) {
            // Command failed or unsupported; can't get drive state
            return;
        }

        // Parse header
        const uint16 respLength = util::ReadBE<uint16>(&buffer[0]);

        // Check that the Media Event class is supported
        const bool noEventAvailable = bit::test<7>(buffer[1]);
        if (noEventAvailable) {
            return;
        }

        // Check that we actually got a Media Event
        const uint8 notificationClass = bit::extract<0, 2>(buffer[2]);
        if (notificationClass != 0x4) {
            return;
        }

        // Optional check: make sure the Media Event class is reported as supported
        /*const bool mediaEventClassSupported = bit::test<4>(buffer[3]);
        if (!mediaEventClassSupported) {
            return;
        }*/

        // Media Event uses at least 4 bytes, plus 2 from the rest of the header
        if (respLength < 6) {
            return;
        }

        // The second byte of the media event payload has the information we need (Media Status field)
        const bool trayOpen = bit::test<0>(buffer[5]);
        const bool mediaPresent = bit::test<1>(buffer[5]);
        trayState = trayOpen ? ymir::scsi::TrayState::Open : ymir::scsi::TrayState::Closed;
        mediaPresenceState =
            mediaPresent ? ymir::scsi::MediaPresenceState::Present : ymir::scsi::MediaPresenceState::Absent;

        // Double-check that there really is no media
        /*if (!mediaPresent) {
            // TODO: use different method to check for media presence if necessary
            if (...) {
                mediaPresenceState = ymir::scsi::MediaPresenceState::Present;
            }
        }*/
    }
};

struct LinuxCDDeviceManager {
private:
    LinuxCDDeviceManager() {}
    LinuxCDDeviceManager(const LinuxCDDeviceManager &) = delete;
    LinuxCDDeviceManager(LinuxCDDeviceManager &&) = delete;

    static LinuxCDDeviceManager s_instance;

public:
    static LinuxCDDeviceManager &Instance() {
        return s_instance;
    }

    void Reenumerate() {
        std::unique_lock lock{mtxDevs};
        namespace fs = std::filesystem;

        // Iterate over generic SCSI devices looking for type 5 (ROM, as in CD-ROM or DVD-ROM)
        const fs::path sgPath = "/sys/class/scsi_generic/";
        if (!fs::exists(sgPath)) {
            // Can't find path, bail out
            YMIR_DEV_CHECK(); // really shouldn't happen, but wouldn't be surprised if it did happen
            return;
        }

        for (const auto &devEntry : fs::directory_iterator(sgPath)) {
            std::string devName = devEntry.path().filename().string();

            fs::path typePath = devEntry.path() / "device/type";
            if (!fs::exists(typePath)) {
                continue;
            }

            std::ifstream typeFile(typePath);
            int deviceType = -1;
            if (typeFile >> deviceType) {
                if (deviceType == 5) {
                    std::string path = "/dev/" + devName;
                    devices[path] = std::make_unique<LinuxCDDevice>(path);
                }
            }
        }

        PrintDrives();
    }

    std::mutex mtxDevs;

    std::unordered_map<std::string, std::unique_ptr<LinuxCDDevice>> devices;
};

LinuxCDDeviceManager LinuxCDDeviceManager::s_instance{};

// -----------------------------------------------------------------------------

static void PrintDrives() {
    auto &mgr = LinuxCDDeviceManager::Instance();

    fmt::println("Current drives:");
    for (const auto &[path, dev] : mgr.devices) {
        std::string_view mediaPresenceStateStr = [&] {
            switch (dev->mediaPresenceState) {
            case ymir::scsi::MediaPresenceState::Absent: return "no media";
            case ymir::scsi::MediaPresenceState::Present: return "media present";
            case ymir::scsi::MediaPresenceState::Unknown: return "unknown media state";
            default: return "invalid media state";
            }
        }();
        std::string_view trayStateStr = [&] {
            switch (dev->trayState) {
            case ymir::scsi::TrayState::Open: return "tray open";
            case ymir::scsi::TrayState::Closed: return "tray closed";
            case ymir::scsi::TrayState::Unknown: return "unknown tray state";
            default: return "invalid tray state";
            }
        }();
        fmt::memory_buffer buf{};
        auto out = std::back_inserter(buf);
        fmt::format_to(out, "  {}", path);
        fmt::format_to(out, " ({}, {})", mediaPresenceStateStr, trayStateStr);
        fmt::println("{}", std::string_view(buf.begin(), buf.end()));
    }
}

void runCDDeviceEventsSandbox() {
    auto &mgr = LinuxCDDeviceManager::Instance();

    mgr.Reenumerate();

    fmt::println("Press ENTER to exit");
    std::cin.get();
}
